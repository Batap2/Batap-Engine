#pragma once

#include <volk.h>

#include "Assets/AssetHandle.h"
#include "Handles.h"
#include "Renderer/EngineConfig.h"

#include "entt/entt.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace batap
{
struct VulkanContext;
struct ResourceManager;
struct AssetManager;
struct TerrainPass;

struct RtScene
{
    RtScene(VulkanContext& ctx, ResourceManager& resources);
    ~RtScene();

    RtScene(const RtScene&) = delete;
    RtScene& operator=(const RtScene&) = delete;

    void updateBlases(VkCommandBuffer cmd, uint32_t frame, entt::registry& reg,
                      AssetManager& assets, const TerrainPass& terrain);

   private:
    struct Blas
    {
        VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
        GPUResourceHandle buffer_;
        GPUResourceHandle source_;
    };

    struct BlasInputs;
    std::vector<Blas> buildBlases(VkCommandBuffer cmd, const BlasInputs& inputs);

    VulkanContext& ctx_;
    ResourceManager& resources_;
    std::unordered_map<MeshHandle, Blas> meshBlases_;
    std::unordered_map<uint64_t, Blas> chunkBlases_;
    // Destroyed when their frame slot comes back: frames in flight may still
    // trace them.
    std::array<std::vector<Blas>, FramesInFlight> retired_;
};
}  // namespace batap
