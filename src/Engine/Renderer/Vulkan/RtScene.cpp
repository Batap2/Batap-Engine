#include "RtScene.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Mesh_C.h"
#include "Renderer/Vulkan/Passes/TerrainPass.h"
#include "Renderer/Vulkan/VulkanBarrier.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanFormats.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <emhash/hash_set8.hpp>

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
}  // namespace

// Each BLAS is a run of geometries, starting at its firstGeometry_.
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
{}

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

    const std::vector<Blas> built = buildBlases(cmd, inputs);
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        Blas blas = built[i];
        blas.source_ = assets.get(meshes[i])->buffer_;
        meshBlases_.emplace(meshes[i], blas);
    }
    for (size_t i = 0; i < chunks.size(); ++i)
        chunkBlases_.emplace(chunks[i], built[meshes.size() + i]);
}

std::vector<RtScene::Blas> RtScene::buildBlases(VkCommandBuffer cmd, const BlasInputs& inputs)
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

        Blas& blas = built[i];
        blas.buffer_ =
            resources_.createAccelerationStructureBuffer(sizes.accelerationStructureSize);

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = resources_.bufferFor(blas.buffer_);
        createInfo.size = sizes.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        if (vkCreateAccelerationStructureKHR(ctx_.device_, &createInfo, nullptr, &blas.handle_) !=
            VK_SUCCESS)
            throw std::runtime_error("RtScene : BLAS");
        info.dstAccelerationStructure = blas.handle_;

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

    vkCmdBuildAccelerationStructuresKHR(cmd, static_cast<uint32_t>(count), infos.data(),
                                        rangePointers.data());
    BarrierBatch{}.memory(Usage::AccelerationStructureBuild, Usage::AnyRead).flush(cmd);
    resources_.requestDestroy(scratch);

    std::cout << "[RT] " << count << " BLAS : " << triangles << " triangles, " << bytes
              << " octets (scratch " << scratchBytes << ")\n";
    return built;
}
}  // namespace batap
