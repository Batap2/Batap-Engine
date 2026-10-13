#include "RtScene.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Mesh_C.h"
#include "Renderer/Vulkan/VulkanBarrier.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Renderer/Vulkan/VulkanFormats.h"
#include "Renderer/Vulkan/VulkanResources.h"

#include <algorithm>
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
}  // namespace

RtScene::RtScene(VulkanContext& ctx, ResourceManager& resources) : ctx_(ctx), resources_(resources)
{}

// The renderer has waited for the device to idle; the buffers go with the
// ResourceManager.
RtScene::~RtScene()
{
    for (const auto& [mesh, blas] : blases_)
        vkDestroyAccelerationStructureKHR(ctx_.device_, blas.handle_, nullptr);
    for (const auto& slot : retired_)
        for (const Blas& blas : slot)
            vkDestroyAccelerationStructureKHR(ctx_.device_, blas.handle_, nullptr);
}

void RtScene::updateBlases(VkCommandBuffer cmd, uint32_t frame, entt::registry& reg,
                           AssetManager& assets)
{
    for (const Blas& blas : retired_[frame])
    {
        vkDestroyAccelerationStructureKHR(ctx_.device_, blas.handle_, nullptr);
        resources_.requestDestroy(blas.buffer_);
    }
    retired_[frame].clear();

    std::erase_if(blases_,
                  [&](const auto& entry)
                  {
                      const Mesh* mesh = assets.get(entry.first);
                      if (mesh && mesh->buffer_ == entry.second.source_)
                          return false;
                      retired_[frame].push_back(entry.second);
                      return true;
                  });

    std::vector<MeshHandle> pending;
    reg.view<Mesh_C>().each(
        [&](const Mesh_C& meshC)
        {
            if (!meshC.mesh_ || blases_.contains(meshC.mesh_) ||
                std::find(pending.begin(), pending.end(), meshC.mesh_) != pending.end())
                return;
            const Mesh* mesh = assets.get(meshC.mesh_);
            if (mesh && mesh->isRenderable() && mesh->indexCount_ >= 3)
                pending.push_back(meshC.mesh_);
        });
    if (pending.empty())
        return;

    // One geometry per submesh, so a hit's geometry index is its submesh.
    std::vector<VkAccelerationStructureGeometryKHR> geometries;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
    std::vector<uint32_t> primitiveCounts;
    std::vector<uint32_t> firstGeometry;
    for (const MeshHandle handle : pending)
    {
        const Mesh* mesh = assets.get(handle);
        const VkDeviceAddress address = resources_.deviceAddress(mesh->buffer_);

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        auto& triangles = geometry.geometry.triangles;
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = address + mesh->streamOffsets_[Mesh::Position];
        triangles.vertexStride = sizeof(float) * 3;
        triangles.maxVertex = mesh->vertexCount_ - 1;
        triangles.indexType = toVkIndexType(mesh->indexFormat_);
        triangles.indexData.deviceAddress = address + mesh->streamOffsets_[Mesh::Index];

        const uint32_t indexBytes = bytesPerPixel(mesh->indexFormat_);
        const auto addGeometry = [&](uint32_t indexOffset, uint32_t indexCount)
        {
            geometries.push_back(geometry);
            VkAccelerationStructureBuildRangeInfoKHR range{};
            range.primitiveCount = indexCount / 3;
            range.primitiveOffset = indexOffset * indexBytes;
            ranges.push_back(range);
            primitiveCounts.push_back(range.primitiveCount);
        };

        firstGeometry.push_back(static_cast<uint32_t>(geometries.size()));
        if (mesh->subMeshCount == 0)
            addGeometry(0, mesh->indexCount_);
        for (uint8_t sub = 0; sub < mesh->subMeshCount; ++sub)
            addGeometry(mesh->subMeshes[sub].indexOffset, mesh->subMeshes[sub].indexCount);
    }
    firstGeometry.push_back(static_cast<uint32_t>(geometries.size()));

    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos(pending.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePointers(pending.size());
    std::vector<uint64_t> scratchOffsets(pending.size());
    uint64_t scratchBytes = 0;
    for (size_t i = 0; i < pending.size(); ++i)
    {
        const uint32_t first = firstGeometry[i];
        VkAccelerationStructureBuildGeometryInfoKHR& info = infos[i];
        info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                     VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.geometryCount = firstGeometry[i + 1] - first;
        info.pGeometries = &geometries[first];
        rangePointers[i] = &ranges[first];

        VkAccelerationStructureBuildSizesInfoKHR sizes{};
        sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(ctx_.device_,
                                                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &info, &primitiveCounts[first], &sizes);

        Blas blas;
        blas.source_ = assets.get(pending[i])->buffer_;
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

        uint64_t triangleCount = 0;
        for (uint32_t g = first; g < firstGeometry[i + 1]; ++g)
            triangleCount += primitiveCounts[g];
        std::cout << "[RT] BLAS : " << triangleCount << " triangles, "
                  << sizes.accelerationStructureSize << " octets (scratch "
                  << sizes.buildScratchSize << ")\n";

        blases_.emplace(pending[i], blas);
    }

    const GPUResourceHandle scratch = resources_.createScratchBuffer(scratchBytes);
    const VkDeviceAddress scratchAddress = resources_.deviceAddress(scratch);
    for (size_t i = 0; i < infos.size(); ++i)
        infos[i].scratchData.deviceAddress = scratchAddress + scratchOffsets[i];

    vkCmdBuildAccelerationStructuresKHR(cmd, static_cast<uint32_t>(infos.size()), infos.data(),
                                        rangePointers.data());
    BarrierBatch{}.memory(Usage::AccelerationStructureBuild, Usage::AnyRead).flush(cmd);
    resources_.requestDestroy(scratch);
}
}  // namespace batap
