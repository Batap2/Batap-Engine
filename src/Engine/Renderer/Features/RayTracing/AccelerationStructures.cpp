#include "Renderer/Features/RayTracing/AccelerationStructures.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Mesh_C.h"
#include "Components/Transform_C.h"
#include "EigenTypes.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Device/Formats.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/Usage.h"
#include "Renderer/Device/VulkanContext.h"
#include "Renderer/Draws/TerrainDrawer.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Shaders/ShaderInterop.h"

#include <emhash/hash_set8.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace batap
{
namespace
{
constexpr uint64_t kTracedStride = sizeof(float) * 3;
constexpr VkBuildAccelerationStructureFlagsKHR kRefittedFlags =
    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
constexpr VkBuildAccelerationStructureFlagsKHR kCompactedFlags =
    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;

uint64_t alignUp(uint64_t v, uint64_t a)
{
    return (v + a - 1) / a * a;
}

VkAccelerationStructureGeometryKHR triangleGeometry(VkDeviceAddress vertices, uint64_t vertexStride,
                                                    uint64_t vertexCount, VkIndexType indexType,
                                                    VkDeviceAddress indices)
{
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& triangles = geometry.geometry.triangles;
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertices;
    triangles.vertexStride = vertexStride;
    triangles.maxVertex = static_cast<uint32_t>(vertexCount - 1);
    triangles.indexType = indexType;
    triangles.indexData.deviceAddress = indices;
    return geometry;
}

VkAccelerationStructureGeometryKHR instancesGeometry(VkDeviceAddress instances)
{
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = instances;
    return geometry;
}

VkAccelerationStructureBuildGeometryInfoKHR
tlasBuildInfo(const VkAccelerationStructureGeometryKHR& geometry)
{
    VkAccelerationStructureBuildGeometryInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.geometryCount = 1;
    info.pGeometries = &geometry;
    return info;
}

VkAccelerationStructureInstanceKHR tlasInstance(const m4f& world, uint32_t customIndex,
                                                uint32_t mask, VkDeviceAddress blas)
{
    const Eigen::Matrix<float, 3, 4, Eigen::RowMajor> rows = world.topRows<3>();
    VkAccelerationStructureInstanceKHR instance{};
    std::memcpy(&instance.transform, rows.data(), sizeof(instance.transform));
    instance.instanceCustomIndex = customIndex & 0xFFFFFF;
    instance.mask = mask;
    instance.accelerationStructureReference = blas;
    return instance;
}

template <class Fn>
void forEachSubmesh(const Mesh& mesh, Fn&& fn)
{
    const uint32_t indexBytes = bytesPerPixel(mesh.indexFormat_);
    if (mesh.subMeshCount == 0)
        fn(0u, mesh.indexCount_);
    else
        for (uint8_t sub = 0; sub < mesh.subMeshCount; ++sub)
            fn(mesh.subMeshes[sub].indexOffset * indexBytes, mesh.subMeshes[sub].indexCount);
}

RtGeometryGPUData geometryRecord(VkDeviceAddress indices, VkDeviceAddress vertices,
                                 uint32_t vertexStride, uint32_t indexBytes)
{
    RtGeometryGPUData record{};
    record.indicesAddrLo_ = static_cast<uint32_t>(indices);
    record.indicesAddrHi_ = static_cast<uint32_t>(indices >> 32);
    record.verticesAddrLo_ = static_cast<uint32_t>(vertices);
    record.verticesAddrHi_ = static_cast<uint32_t>(vertices >> 32);
    record.vertexStride_ = vertexStride;
    record.indexBytes_ = indexBytes;
    return record;
}

}  // namespace

struct AccelerationStructures::BlasInputs
{
    std::vector<VkAccelerationStructureGeometryKHR> geometries_;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges_;
    std::vector<uint32_t> firstGeometry_;
    std::vector<bool> refitted_;

    void beginBlas(bool refitted)
    {
        firstGeometry_.push_back(static_cast<uint32_t>(geometries_.size()));
        refitted_.push_back(refitted);
    }

    void addGeometry(const VkAccelerationStructureGeometryKHR& geometry, uint32_t primitiveOffset,
                     uint32_t primitiveCount)
    {
        geometries_.push_back(geometry);
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveOffset = primitiveOffset;
        range.primitiveCount = primitiveCount;
        ranges_.push_back(range);
    }
};

AccelerationStructures::AccelerationStructures(VulkanContext& ctx, ResourceManager& resources)
    : ctx_(ctx), resources_(resources)
{}

AccelerationStructures::Buffers
AccelerationStructures::addPasses(RenderGraph& graph, GraphBuffer uploads, GraphBuffer traced,
                                  uint32_t frame, entt::registry& reg,
                                  GPUInstanceManager& instances, AssetManager& assets,
                                  const TerrainDrawer& terrain, entt::entity camera)
{
    Buffers buffers;
    buffers.blases_ = graph.importBuffer("blases", VK_NULL_HANDLE, Usage::AccelerationStructureRead,
                                         Usage::AccelerationStructureRead);
    updateBlases(graph, buffers.blases_, uploads, traced, frame, reg, assets, terrain);
    buffers.tlas_ =
        buildTlas(graph, buffers.blases_, frame, reg, instances, assets, terrain, camera);
    return buffers;
}

// The renderer has waited for the device to idle; the buffers go with the
// ResourceManager.
AccelerationStructures::~AccelerationStructures()
{
    for (const auto& [mesh, blas] : meshBlases_)
        vkDestroyAccelerationStructureKHR(ctx_.device_, blas.handle_, nullptr);
    for (const auto& [chunk, blas] : chunkBlases_)
        vkDestroyAccelerationStructureKHR(ctx_.device_, blas.handle_, nullptr);
    for (const auto& slot : retired_)
        for (const Blas& blas : slot)
            vkDestroyAccelerationStructureKHR(ctx_.device_, blas.handle_, nullptr);
    for (const CompactionQueries& queries : queries_)
        vkDestroyQueryPool(ctx_.device_, queries.pool_, nullptr);
    for (const Tlas& tlas : tlases_)
        vkDestroyAccelerationStructureKHR(ctx_.device_, tlas.handle_, nullptr);
}

void AccelerationStructures::updateBlases(RenderGraph& graph, GraphBuffer blases,
                                          GraphBuffer uploads, GraphBuffer traced, uint32_t frame,
                                          entt::registry& reg, AssetManager& assets,
                                          const TerrainDrawer& terrain)
{
    for (const Blas& blas : retired_[frame])
    {
        vkDestroyAccelerationStructureKHR(ctx_.device_, blas.handle_, nullptr);
        resources_.requestDestroy(blas.buffer_);
    }
    retired_[frame].clear();

    std::erase_if(meshBlases_,
                  [&](const auto& entry)
                  {
                      const Mesh* mesh = assets.get(entry.first);
                      if (mesh && mesh->buffer_ == entry.second.source_)
                          return false;
                      retired_[frame].push_back(entry.second);
                      return true;
                  });
    std::erase_if(chunkBlases_,
                  [&](const auto& entry)
                  {
                      if (terrain.slots().contains(entry.first))
                          return false;
                      retired_[frame].push_back(entry.second);
                      return true;
                  });

    compactBlases(graph, blases, frame);

    // One geometry per submesh, or per stratum for a chunk: a hit's geometry
    // index names its material.
    BlasInputs inputs;

    std::vector<MeshHandle> meshes;
    emhash8::HashSet<MeshHandle> seen;
    reg.view<Mesh_C>().each(
        [&](const Mesh_C& meshC)
        {
            if (!meshC.mesh_ || !meshC.visible_ || meshBlases_.contains(meshC.mesh_) ||
                !seen.insert(meshC.mesh_).second)
                return;
            const Mesh* mesh = assets.get(meshC.mesh_);
            if (!mesh || !mesh->isRenderable() || mesh->indexCount_ < 3)
                return;
            meshes.push_back(meshC.mesh_);

            const VkDeviceAddress address = resources_.deviceAddress(mesh->buffer_);
            const VkAccelerationStructureGeometryKHR geometry =
                triangleGeometry(address + mesh->streamOffsets_[Mesh::Position], sizeof(float) * 3,
                                 mesh->vertexCount_, toVkIndexType(mesh->indexFormat_),
                                 address + mesh->streamOffsets_[Mesh::Index]);
            inputs.beginBlas(false);
            forEachSubmesh(*mesh, [&](uint32_t offset, uint32_t count)
                           { inputs.addGeometry(geometry, offset, count / 3); });
        });

    std::vector<uint64_t> chunks;
    for (const auto& [id, slot] : terrain.slots())
    {
        if (!slot.shown_ || !slot.tracedReady_ || chunkBlases_.contains(id))
            continue;
        chunks.push_back(id);
        inputs.beginBlas(true);
        addChunkGeometries(inputs, terrain, id);
    }

    if (!inputs.firstGeometry_.empty())
    {
        const std::vector<Blas> built =
            buildBlases(graph, blases, uploads, traced, frame, std::move(inputs));
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            Blas blas = built[i];
            blas.source_ = assets.get(meshes[i])->buffer_;
            meshBlases_.emplace(meshes[i], blas);
        }
        for (size_t i = 0; i < chunks.size(); ++i)
            chunkBlases_.emplace(chunks[i], built[meshes.size() + i]);
    }
    refitChunks(graph, blases, traced, terrain, chunks);
}

void AccelerationStructures::addChunkGeometries(BlasInputs& inputs, const TerrainDrawer& terrain,
                                                uint64_t chunk) const
{
    const TerrainDrawer::Slot& slot = terrain.slots().at(chunk);
    const VkAccelerationStructureGeometryKHR geometry = triangleGeometry(
        resources_.deviceAddress(terrain.tracedBuffer()) + slot.traced_ * kTracedStride,
        kTracedStride, slot.vertices_, VK_INDEX_TYPE_UINT16,
        resources_.deviceAddress(terrain.indexBuffer()) + slot.index_ * sizeof(uint16_t));
    for (const StratumRange& range : slot.ranges_)
        inputs.addGeometry(geometry, range.offset_ * sizeof(uint16_t), range.count_ / 3);
}

void AccelerationStructures::refitChunks(RenderGraph& graph, GraphBuffer blases, GraphBuffer traced,
                                         const TerrainDrawer& terrain,
                                         const std::vector<uint64_t>& built)
{
    BlasInputs inputs;
    std::vector<VkAccelerationStructureKHR> handles;
    std::vector<uint64_t> scratchOffsets;
    uint64_t scratchBytes = 0;
    for (const uint64_t id : terrain.morphed())
    {
        const auto blas = chunkBlases_.find(id);
        if (blas == chunkBlases_.end() || std::ranges::find(built, id) != built.end())
            continue;
        inputs.beginBlas(true);
        addChunkGeometries(inputs, terrain, id);
        handles.push_back(blas->second.handle_);
        scratchOffsets.push_back(scratchBytes);
        scratchBytes += alignUp(blas->second.updateScratch_, ctx_.asScratchAlignment_);
    }
    if (handles.empty())
        return;

    const GPUResourceHandle scratch = resources_.createScratchBuffer(scratchBytes);
    const VkDeviceAddress scratchAddress = resources_.deviceAddress(scratch);
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos(handles.size());
    for (size_t i = 0; i < handles.size(); ++i)
    {
        VkAccelerationStructureBuildGeometryInfoKHR& info = infos[i];
        info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        info.flags = kRefittedFlags;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        info.srcAccelerationStructure = handles[i];
        info.dstAccelerationStructure = handles[i];
        info.scratchData.deviceAddress = scratchAddress + scratchOffsets[i];
    }

    const GraphBuffer scratchBuffer =
        graph.importBuffer("blas refit scratch", resources_, scratch, Usage::None, Usage::None);
    resources_.requestDestroy(scratch);
    graph.addPass("blas refit", GraphQueue::Compute)
        .read(traced, Usage::AnyRead)
        .write(blases, Usage::AccelerationStructureBuild)
        .write(scratchBuffer, Usage::AccelerationStructureBuild)
        .execute(
            [geometries = std::move(inputs), buildInfos = std::move(infos)](GraphContext& c) mutable
            {
                std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ranges(
                    buildInfos.size());
                for (size_t i = 0; i < buildInfos.size(); ++i)
                {
                    const uint32_t first = geometries.firstGeometry_[i];
                    const uint32_t end = i + 1 < buildInfos.size()
                                             ? geometries.firstGeometry_[i + 1]
                                             : static_cast<uint32_t>(geometries.geometries_.size());
                    buildInfos[i].geometryCount = end - first;
                    buildInfos[i].pGeometries = &geometries.geometries_[first];
                    ranges[i] = &geometries.ranges_[first];
                }
                vkCmdBuildAccelerationStructuresKHR(c.cmd(),
                                                    static_cast<uint32_t>(buildInfos.size()),
                                                    buildInfos.data(), ranges.data());
            });
}

VkAccelerationStructureKHR
AccelerationStructures::createAccelerationStructure(GPUResourceHandle buffer, uint64_t size,
                                                    VkAccelerationStructureTypeKHR type)
{
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = resources_.bufferFor(buffer);
    createInfo.size = size;
    createInfo.type = type;
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    if (vkCreateAccelerationStructureKHR(ctx_.device_, &createInfo, nullptr, &handle) != VK_SUCCESS)
        throw std::runtime_error("AccelerationStructures : acceleration structure");
    return handle;
}

AccelerationStructures::Blas AccelerationStructures::createBlas(uint64_t size)
{
    Blas blas;
    blas.size_ = size;
    blas.buffer_ = resources_.createAccelerationStructureBuffer(size);
    blas.handle_ = createAccelerationStructure(blas.buffer_, size,
                                               VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = blas.handle_;
    blas.address_ = vkGetAccelerationStructureDeviceAddressKHR(ctx_.device_, &addressInfo);
    return blas;
}

std::vector<AccelerationStructures::Blas>
AccelerationStructures::buildBlases(RenderGraph& graph, GraphBuffer blases, GraphBuffer uploads,
                                    GraphBuffer traced, uint32_t frame, BlasInputs inputs)
{
    const size_t count = inputs.firstGeometry_.size();
    const auto endGeometry = [&](size_t i)
    {
        return i + 1 < count ? inputs.firstGeometry_[i + 1]
                             : static_cast<uint32_t>(inputs.geometries_.size());
    };

    std::vector<uint32_t> primitiveCounts;
    primitiveCounts.reserve(inputs.ranges_.size());
    for (const VkAccelerationStructureBuildRangeInfoKHR& range : inputs.ranges_)
        primitiveCounts.push_back(range.primitiveCount);

    std::vector<Blas> built(count);
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos(count);
    std::vector<uint64_t> scratchOffsets(count);
    uint64_t scratchBytes = 0;
    uint64_t triangles = 0;
    uint64_t bytes = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t first = inputs.firstGeometry_[i];
        VkAccelerationStructureBuildGeometryInfoKHR& info = infos[i];
        info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        info.flags = inputs.refitted_[i] ? kRefittedFlags : kCompactedFlags;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.geometryCount = endGeometry(i) - first;
        info.pGeometries = &inputs.geometries_[first];

        VkAccelerationStructureBuildSizesInfoKHR sizes{};
        sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(ctx_.device_,
                                                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &info, &primitiveCounts[first], &sizes);

        built[i] = createBlas(sizes.accelerationStructureSize);
        built[i].updateScratch_ = sizes.updateScratchSize;
        info.dstAccelerationStructure = built[i].handle_;

        scratchOffsets[i] = scratchBytes;
        scratchBytes += alignUp(sizes.buildScratchSize, ctx_.asScratchAlignment_);
        bytes += sizes.accelerationStructureSize;
        for (uint32_t g = first; g < endGeometry(i); ++g)
            triangles += primitiveCounts[g];
    }

    const GPUResourceHandle scratch = resources_.createScratchBuffer(scratchBytes);
    const VkDeviceAddress scratchAddress = resources_.deviceAddress(scratch);
    for (size_t i = 0; i < count; ++i)
        infos[i].scratchData.deviceAddress = scratchAddress + scratchOffsets[i];

    std::vector<VkAccelerationStructureKHR> compacted;
    for (size_t i = 0; i < count; ++i)
        if (!inputs.refitted_[i])
        {
            built[i].query_ = static_cast<uint32_t>(compacted.size());
            built[i].querySlot_ = frame;
            compacted.push_back(built[i].handle_);
        }

    CompactionQueries& queries = queries_[frame];
    assert(queries.count_ == 0 && "buildBlases: compactBlases reads the slot's queries first");
    if (compacted.size() > queries.capacity_)
    {
        vkDestroyQueryPool(ctx_.device_, queries.pool_, nullptr);
        queries.capacity_ =
            std::max(static_cast<uint32_t>(compacted.size()), queries.capacity_ * 2);
        VkQueryPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        poolInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        poolInfo.queryCount = queries.capacity_;
        if (vkCreateQueryPool(ctx_.device_, &poolInfo, nullptr, &queries.pool_) != VK_SUCCESS)
            throw std::runtime_error("AccelerationStructures : query pool");
    }

    queries.count_ = static_cast<uint32_t>(compacted.size());

    const GraphBuffer scratchBuffer =
        graph.importBuffer("blas scratch", resources_, scratch, Usage::None, Usage::None);
    resources_.requestDestroy(scratch);
    graph.addPass("blas build", GraphQueue::Compute)
        .read(uploads, Usage::AnyRead)
        .read(traced, Usage::AnyRead)
        .write(blases, Usage::AccelerationStructureBuild)
        .write(scratchBuffer, Usage::AccelerationStructureBuild)
        .execute(
            [geometries = std::move(inputs), buildInfos = std::move(infos), pool = queries.pool_,
             queried = queries.count_](GraphContext& c) mutable
            {
                // Into this lambda's own inputs: std::function may have copied it.
                std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ranges(
                    buildInfos.size());
                for (size_t i = 0; i < buildInfos.size(); ++i)
                {
                    const uint32_t first = geometries.firstGeometry_[i];
                    buildInfos[i].pGeometries = &geometries.geometries_[first];
                    ranges[i] = &geometries.ranges_[first];
                }
                if (queried > 0)
                    vkCmdResetQueryPool(c.cmd(), pool, 0, queried);
                vkCmdBuildAccelerationStructuresKHR(c.cmd(),
                                                    static_cast<uint32_t>(buildInfos.size()),
                                                    buildInfos.data(), ranges.data());
            });
    if (!compacted.empty())
        graph.addPass("blas compacted sizes", GraphQueue::Compute)
            .read(blases, Usage::AccelerationStructureBuild)
            .execute(
                [blasHandles = std::move(compacted), pool = queries.pool_](GraphContext& c)
                {
                    vkCmdWriteAccelerationStructuresPropertiesKHR(
                        c.cmd(), static_cast<uint32_t>(blasHandles.size()), blasHandles.data(),
                        VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, pool, 0);
                });

    std::cout << "[RT] " << count << " BLAS : " << triangles << " triangles, " << bytes
              << " octets (scratch " << scratchBytes << ")\n";
    return built;
}

void AccelerationStructures::compactBlases(RenderGraph& graph, GraphBuffer blases, uint32_t frame)
{
    CompactionQueries& queries = queries_[frame];
    if (queries.count_ == 0)
        return;
    // Written by the slot's previous frame, which beginFrame has waited for.
    std::vector<VkDeviceSize> sizes(queries.count_);
    vkGetQueryPoolResults(ctx_.device_, queries.pool_, 0, queries.count_,
                          sizes.size() * sizeof(VkDeviceSize), sizes.data(), sizeof(VkDeviceSize),
                          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    queries.count_ = 0;

    std::vector<VkCopyAccelerationStructureInfoKHR> copies;
    uint64_t before = 0;
    uint64_t after = 0;
    const auto compact = [&](Blas& blas)
    {
        if (!blas.query_ || blas.querySlot_ != frame)
            return;
        Blas compacted = createBlas(sizes[*blas.query_]);
        compacted.source_ = blas.source_;

        VkCopyAccelerationStructureInfoKHR& copy = copies.emplace_back();
        copy.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
        copy.src = blas.handle_;
        copy.dst = compacted.handle_;
        copy.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;

        before += blas.size_;
        after += compacted.size_;
        retired_[frame].push_back(blas);
        blas = compacted;
    };
    for (auto& [mesh, blas] : meshBlases_)
        compact(blas);
    for (auto& [chunk, blas] : chunkBlases_)
        compact(blas);
    if (copies.empty())
        return;

    std::cout << "[RT] compaction de " << copies.size() << " BLAS : " << before << " -> " << after
              << " octets\n";
    graph.addPass("blas compaction", GraphQueue::Compute)
        .write(blases, Usage::AccelerationStructureBuild)
        .execute(
            [compactions = std::move(copies)](GraphContext& c)
            {
                for (const VkCopyAccelerationStructureInfoKHR& copy : compactions)
                    vkCmdCopyAccelerationStructureKHR(c.cmd(), &copy);
            });
}

GraphBuffer AccelerationStructures::buildTlas(RenderGraph& graph, GraphBuffer blases,
                                              uint32_t frame, entt::registry& reg,
                                              GPUInstanceManager& instances, AssetManager& assets,
                                              const TerrainDrawer& terrain, entt::entity camera)
{
    // Relative to the render camera
    const v3d eye = reg.get<Transform_C>(camera).world().translation().cast<double>();

    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
    std::vector<RtInstanceGPUData> hitInstances;
    std::vector<RtGeometryGPUData> geometries;
    std::unordered_map<MeshHandle, uint32_t> meshGeometries;
    reg.view<Mesh_C, Transform_C>().each(
        [&](entt::entity e, const Mesh_C& meshC, const Transform_C& transform)
        {
            if (!meshC.visible_)
                return;
            const auto blas = meshBlases_.find(meshC.mesh_);
            if (blas == meshBlases_.end())
                return;
            const auto id = instances.pool<StaticMeshInstance>().getGPUIndex({&reg, e});
            if (!id.valid())
                return;
            const Mesh* mesh = assets.get(meshC.mesh_);
            if (!mesh)
                return;
            const auto [first, added] =
                meshGeometries.try_emplace(meshC.mesh_, static_cast<uint32_t>(geometries.size()));
            if (added)
            {
                const VkDeviceAddress address = resources_.deviceAddress(mesh->buffer_);
                const VkDeviceAddress indices = address + mesh->streamOffsets_[Mesh::Index];
                const VkDeviceAddress vertices = address + mesh->streamOffsets_[Mesh::Position];
                const uint32_t indexBytes = bytesPerPixel(mesh->indexFormat_);
                forEachSubmesh(*mesh,
                               [&](uint32_t offset, uint32_t)
                               {
                                   geometries.push_back(geometryRecord(
                                       indices + offset, vertices, sizeof(float) * 3, indexBytes));
                               });
            }
            m4f world = transform.worldMatrix();
            world.topRightCorner<3, 1>() =
                (transform.world().translation().cast<double>() - eye).cast<float>();
            const uint32_t mask = meshC.castShadows_ ? TlasMaskShadowCaster : TlasMaskNoShadow;
            tlasInstances.push_back(tlasInstance(world, id, mask, blas->second.address_));
            hitInstances.push_back({first->second, id, RtInstanceMesh, 0});
        });
    const VkDeviceAddress tracedPool = resources_.deviceAddress(terrain.tracedBuffer());
    const VkDeviceAddress indexPool = resources_.deviceAddress(terrain.indexBuffer());
    for (const auto& [id, slot] : terrain.slots())
    {
        if (!slot.shown_)
            continue;
        const auto blas = chunkBlases_.find(id);
        if (blas == chunkBlases_.end())
            continue;
        const m4f world = Eigen::Map<const m4f>(terrain.worlds()[slot.instance_].world_);
        tlasInstances.push_back(
            tlasInstance(world, slot.instance_, TlasMaskShadowCaster, blas->second.address_));
        hitInstances.push_back(
            {static_cast<uint32_t>(geometries.size()), slot.instance_, RtInstanceChunk, 0});
        const VkDeviceAddress vertices = tracedPool + slot.traced_ * kTracedStride;
        const VkDeviceAddress indices = indexPool + slot.index_ * sizeof(uint16_t);
        for (const StratumRange& range : slot.ranges_)
            geometries.push_back(geometryRecord(indices + range.offset_ * sizeof(uint16_t),
                                                vertices, kTracedStride, sizeof(uint16_t)));
    }

    HitTables& tables = hitTables_[frame];
    reserveHitTables(tables, std::max(static_cast<uint32_t>(geometries.size()), 1u),
                     std::max(static_cast<uint32_t>(hitInstances.size()), 1u));
    if (!geometries.empty())
        std::memcpy(resources_.mappedFor(tables.geometries_).data(), geometries.data(),
                    geometries.size() * sizeof(RtGeometryGPUData));
    if (!hitInstances.empty())
        std::memcpy(resources_.mappedFor(tables.instances_).data(), hitInstances.data(),
                    hitInstances.size() * sizeof(RtInstanceGPUData));

    Tlas& tlas = tlases_[frame];
    const auto count = static_cast<uint32_t>(tlasInstances.size());
    if (tlas.handle_ == VK_NULL_HANDLE || count > tlas.capacity_)
        growTlas(tlas, count);
    if (count > 0)
        std::memcpy(resources_.mappedFor(tlas.instances_).data(), tlasInstances.data(),
                    count * sizeof(VkAccelerationStructureInstanceKHR));

    const GraphBuffer tlasBuffer =
        graph.importBuffer("tlas", resources_, tlas.buffer_, Usage::None, Usage::None);
    const GraphBuffer scratch =
        graph.importBuffer("tlas scratch", resources_, tlas.scratch_, Usage::None, Usage::None);
    graph.addPass("tlas build", GraphQueue::Compute)
        .read(blases, Usage::AccelerationStructureBuild)
        .write(tlasBuffer, Usage::AccelerationStructureBuild)
        .write(scratch, Usage::AccelerationStructureBuild)
        .execute(
            [geometry = instancesGeometry(resources_.deviceAddress(tlas.instances_)),
             handle = tlas.handle_, scratchAddress = resources_.deviceAddress(tlas.scratch_),
             count](GraphContext& c)
            {
                VkAccelerationStructureBuildGeometryInfoKHR info = tlasBuildInfo(geometry);
                info.dstAccelerationStructure = handle;
                info.scratchData.deviceAddress = scratchAddress;
                VkAccelerationStructureBuildRangeInfoKHR range{};
                range.primitiveCount = count;
                const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
                vkCmdBuildAccelerationStructuresKHR(c.cmd(), 1, &info, &ranges);
            });

    return tlasBuffer;
}

void AccelerationStructures::reserveHitTables(HitTables& tables, uint32_t geometries,
                                              uint32_t instances)
{
    if (geometries > tables.geometryCapacity_)
    {
        if (tables.geometries_.valid())
            resources_.requestDestroy(tables.geometries_);
        tables.geometryCapacity_ = std::max({geometries, tables.geometryCapacity_ * 2, 256u});
        tables.geometries_ = resources_.createMappedBuffer(uint64_t{tables.geometryCapacity_} *
                                                           sizeof(RtGeometryGPUData));
    }
    if (instances > tables.instanceCapacity_)
    {
        if (tables.instances_.valid())
            resources_.requestDestroy(tables.instances_);
        tables.instanceCapacity_ = std::max({instances, tables.instanceCapacity_ * 2, 64u});
        tables.instances_ = resources_.createMappedBuffer(uint64_t{tables.instanceCapacity_} *
                                                          sizeof(RtInstanceGPUData));
    }
}

// The slot's previous frame is done with the old one.
void AccelerationStructures::growTlas(Tlas& tlas, uint32_t instances)
{
    vkDestroyAccelerationStructureKHR(ctx_.device_, tlas.handle_, nullptr);
    if (tlas.buffer_.valid())
    {
        resources_.requestDestroy(tlas.buffer_);
        resources_.requestDestroy(tlas.scratch_);
        resources_.requestDestroy(tlas.instances_);
    }
    tlas.capacity_ = std::max({instances, tlas.capacity_ * 2, 64u});

    const VkAccelerationStructureGeometryKHR geometry = instancesGeometry(0);
    const VkAccelerationStructureBuildGeometryInfoKHR info = tlasBuildInfo(geometry);
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(ctx_.device_,
                                            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info,
                                            &tlas.capacity_, &sizes);

    tlas.buffer_ = resources_.createAccelerationStructureBuffer(sizes.accelerationStructureSize);
    tlas.handle_ = createAccelerationStructure(tlas.buffer_, sizes.accelerationStructureSize,
                                               VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);
    tlas.scratch_ = resources_.createScratchBuffer(sizes.buildScratchSize);
    tlas.instances_ = resources_.createMappedBuffer(uint64_t{tlas.capacity_} *
                                                    sizeof(VkAccelerationStructureInstanceKHR));

    std::cout << "[RT] TLAS : " << tlas.capacity_ << " instances, "
              << sizes.accelerationStructureSize << " octets (scratch " << sizes.buildScratchSize
              << ")\n";
}
}  // namespace batap
