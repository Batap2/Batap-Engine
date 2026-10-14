#include "RtScene.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Mesh_C.h"
#include "Components/Transform_C.h"
#include "EigenTypes.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Vulkan/Passes/TerrainPass.h"
#include "Renderer/Vulkan/VulkanBarrier.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanFormats.h"
#include "Renderer/Vulkan/VulkanResources.h"
#include "Shaders/ShaderInterop.h"

#include <emhash/hash_set8.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace batap
{
namespace
{
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
}  // namespace

struct RtScene::BlasInputs
{
    std::vector<VkAccelerationStructureGeometryKHR> geometries_;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges_;
    std::vector<uint32_t> firstGeometry_;

    void beginBlas() { firstGeometry_.push_back(static_cast<uint32_t>(geometries_.size())); }

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

RtScene::RtScene(VulkanContext& ctx, ResourceManager& resources) : ctx_(ctx), resources_(resources)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = TlasBinding;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(ctx_.device_, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS)
        throw std::runtime_error("RtScene : set layout");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, FramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = FramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(ctx_.device_, &poolInfo, nullptr, &pool_) != VK_SUCCESS)
        throw std::runtime_error("RtScene : descriptor pool");

    std::array<VkDescriptorSetLayout, FramesInFlight> layouts;
    layouts.fill(setLayout_);
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = pool_;
    setInfo.descriptorSetCount = FramesInFlight;
    setInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(ctx_.device_, &setInfo, sets_.data()) != VK_SUCCESS)
        throw std::runtime_error("RtScene : descriptor sets");
}

// The renderer has waited for the device to idle; the buffers go with the
// ResourceManager.
RtScene::~RtScene()
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
    vkDestroyDescriptorPool(ctx_.device_, pool_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device_, setLayout_, nullptr);
}

void RtScene::updateBlases(VkCommandBuffer cmd, uint32_t frame, entt::registry& reg,
                           AssetManager& assets, const TerrainPass& terrain)
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

    compactBlases(cmd, frame);

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
            const uint32_t indexBytes = bytesPerPixel(mesh->indexFormat_);
            inputs.beginBlas();
            if (mesh->subMeshCount == 0)
                inputs.addGeometry(geometry, 0, mesh->indexCount_ / 3);
            else
                for (uint8_t sub = 0; sub < mesh->subMeshCount; ++sub)
                    inputs.addGeometry(geometry, mesh->subMeshes[sub].indexOffset * indexBytes,
                                       mesh->subMeshes[sub].indexCount / 3);
        });

    std::vector<uint64_t> chunks;
    const VkDeviceAddress vertexPool = resources_.deviceAddress(terrain.vertexBuffer());
    const VkDeviceAddress indexPool = resources_.deviceAddress(terrain.indexBuffer());
    for (const auto& [id, slot] : terrain.slots())
    {
        if (!slot.shown_ || chunkBlases_.contains(id))
            continue;
        chunks.push_back(id);

        const VkAccelerationStructureGeometryKHR geometry = triangleGeometry(
            vertexPool + slot.vertex_ * sizeof(TerrainVertexGPUData), sizeof(TerrainVertexGPUData),
            slot.vertices_, VK_INDEX_TYPE_UINT16, indexPool + slot.index_ * sizeof(uint16_t));
        inputs.beginBlas();
        for (const StratumRange& range : slot.ranges_)
            inputs.addGeometry(geometry, range.offset_ * sizeof(uint16_t), range.count_ / 3);
    }

    if (inputs.firstGeometry_.empty())
        return;

    const std::vector<Blas> built = buildBlases(cmd, frame, inputs);
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        Blas blas = built[i];
        blas.source_ = assets.get(meshes[i])->buffer_;
        meshBlases_.emplace(meshes[i], blas);
    }
    for (size_t i = 0; i < chunks.size(); ++i)
        chunkBlases_.emplace(chunks[i], built[meshes.size() + i]);
}

VkAccelerationStructureKHR RtScene::createAccelerationStructure(GPUResourceHandle buffer,
                                                                uint64_t size,
                                                                VkAccelerationStructureTypeKHR type)
{
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = resources_.bufferFor(buffer);
    createInfo.size = size;
    createInfo.type = type;
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    if (vkCreateAccelerationStructureKHR(ctx_.device_, &createInfo, nullptr, &handle) != VK_SUCCESS)
        throw std::runtime_error("RtScene : acceleration structure");
    return handle;
}

RtScene::Blas RtScene::createBlas(uint64_t size)
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

std::vector<RtScene::Blas> RtScene::buildBlases(VkCommandBuffer cmd, uint32_t frame,
                                                const BlasInputs& inputs)
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
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePointers(count);
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
        info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                     VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.geometryCount = endGeometry(i) - first;
        info.pGeometries = &inputs.geometries_[first];
        rangePointers[i] = &inputs.ranges_[first];

        VkAccelerationStructureBuildSizesInfoKHR sizes{};
        sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(ctx_.device_,
                                                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &info, &primitiveCounts[first], &sizes);

        built[i] = createBlas(sizes.accelerationStructureSize);
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

    CompactionQueries& queries = queries_[frame];
    assert(queries.count_ == 0 && "buildBlases: compactBlases reads the slot's queries first");
    if (count > queries.capacity_)
    {
        vkDestroyQueryPool(ctx_.device_, queries.pool_, nullptr);
        queries.capacity_ = std::max(static_cast<uint32_t>(count), queries.capacity_ * 2);
        VkQueryPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        poolInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        poolInfo.queryCount = queries.capacity_;
        if (vkCreateQueryPool(ctx_.device_, &poolInfo, nullptr, &queries.pool_) != VK_SUCCESS)
            throw std::runtime_error("RtScene : query pool");
    }
    vkCmdResetQueryPool(cmd, queries.pool_, 0, static_cast<uint32_t>(count));

    vkCmdBuildAccelerationStructuresKHR(cmd, static_cast<uint32_t>(count), infos.data(),
                                        rangePointers.data());
    BarrierBatch{}.memory(Usage::AccelerationStructureBuild, Usage::AnyRead).flush(cmd);
    resources_.requestDestroy(scratch);

    std::vector<VkAccelerationStructureKHR> handles(count);
    for (size_t i = 0; i < count; ++i)
    {
        handles[i] = built[i].handle_;
        built[i].query_ = static_cast<uint32_t>(i);
        built[i].querySlot_ = frame;
    }
    vkCmdWriteAccelerationStructuresPropertiesKHR(
        cmd, static_cast<uint32_t>(count), handles.data(),
        VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queries.pool_, 0);
    queries.count_ = static_cast<uint32_t>(count);

    std::cout << "[RT] " << count << " BLAS : " << triangles << " triangles, " << bytes
              << " octets (scratch " << scratchBytes << ")\n";
    return built;
}

void RtScene::compactBlases(VkCommandBuffer cmd, uint32_t frame)
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

    uint32_t count = 0;
    uint64_t before = 0;
    uint64_t after = 0;
    const auto compact = [&](Blas& blas)
    {
        if (!blas.query_ || blas.querySlot_ != frame)
            return;
        Blas compacted = createBlas(sizes[*blas.query_]);
        compacted.source_ = blas.source_;

        VkCopyAccelerationStructureInfoKHR copy{};
        copy.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
        copy.src = blas.handle_;
        copy.dst = compacted.handle_;
        copy.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        vkCmdCopyAccelerationStructureKHR(cmd, &copy);

        ++count;
        before += blas.size_;
        after += compacted.size_;
        retired_[frame].push_back(blas);
        blas = compacted;
    };
    for (auto& [mesh, blas] : meshBlases_)
        compact(blas);
    for (auto& [chunk, blas] : chunkBlases_)
        compact(blas);
    if (count == 0)
        return;

    BarrierBatch{}.memory(Usage::AccelerationStructureBuild, Usage::AnyRead).flush(cmd);
    std::cout << "[RT] compaction de " << count << " BLAS : " << before << " -> " << after
              << " octets\n";
}

void RtScene::buildTlas(VkCommandBuffer cmd, uint32_t frame, entt::registry& reg,
                        GPUInstanceManager& instances, const TerrainPass& terrain,
                        entt::entity camera)
{
    // Relative to the render camera
    const v3d eye = reg.get<Transform_C>(camera).world().translation().cast<double>();

    std::vector<VkAccelerationStructureInstanceKHR> tlasInstances;
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
            m4f world = transform.worldMatrix();
            world.topRightCorner<3, 1>() =
                (transform.world().translation().cast<double>() - eye).cast<float>();
            const uint32_t mask = meshC.castShadows_ ? TlasMaskShadowCaster : TlasMaskNoShadow;
            tlasInstances.push_back(tlasInstance(world, id, mask, blas->second.address_));
        });
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
    }

    Tlas& tlas = tlases_[frame];
    const auto count = static_cast<uint32_t>(tlasInstances.size());
    if (tlas.handle_ == VK_NULL_HANDLE || count > tlas.capacity_)
        growTlas(tlas, count);
    if (count > 0)
        std::memcpy(resources_.mappedFor(tlas.instances_).data(), tlasInstances.data(),
                    count * sizeof(VkAccelerationStructureInstanceKHR));

    const VkAccelerationStructureGeometryKHR geometry =
        instancesGeometry(resources_.deviceAddress(tlas.instances_));
    VkAccelerationStructureBuildGeometryInfoKHR info = tlasBuildInfo(geometry);
    info.dstAccelerationStructure = tlas.handle_;
    info.scratchData.deviceAddress = resources_.deviceAddress(tlas.scratch_);
    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = count;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &info, &ranges);
    BarrierBatch{}.memory(Usage::AccelerationStructureBuild, Usage::AnyRead).flush(cmd);

    VkWriteDescriptorSetAccelerationStructureKHR tlasWrite{};
    tlasWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    tlasWrite.accelerationStructureCount = 1;
    tlasWrite.pAccelerationStructures = &tlas.handle_;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = &tlasWrite;
    write.dstSet = sets_[frame];
    write.dstBinding = TlasBinding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    vkUpdateDescriptorSets(ctx_.device_, 1, &write, 0, nullptr);
}

// The slot's previous frame is done with the old one.
void RtScene::growTlas(Tlas& tlas, uint32_t instances)
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
