#pragma once

#include <volk.h>

#include "Assets/AssetHandle.h"
#include "Handles.h"
#include "Renderer/EngineConfig.h"

#include "entt/entt.hpp"

#include <array>
#include <cstdint>
#include <optional>
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
        uint64_t size_ = 0;
        // Until compacted: its compacted-size query in queries_[querySlot_].
        std::optional<uint32_t> query_;
        uint32_t querySlot_ = 0;
    };

    // Written by a frame slot, read when the slot comes back.
    struct CompactionQueries
    {
        VkQueryPool pool_ = VK_NULL_HANDLE;
        uint32_t capacity_ = 0;
        uint32_t count_ = 0;
    };

    struct BlasInputs;
    Blas createBlas(uint64_t size);
    std::vector<Blas> buildBlases(VkCommandBuffer cmd, uint32_t frame, const BlasInputs& inputs);
    void compactBlases(VkCommandBuffer cmd, uint32_t frame);

    VulkanContext& ctx_;
    ResourceManager& resources_;
    std::unordered_map<MeshHandle, Blas> meshBlases_;
    std::unordered_map<uint64_t, Blas> chunkBlases_;
    // Destroyed when their frame slot comes back: frames in flight may still
    // trace them.
    std::array<std::vector<Blas>, FramesInFlight> retired_;
    std::array<CompactionQueries, FramesInFlight> queries_;
};
}  // namespace batap
