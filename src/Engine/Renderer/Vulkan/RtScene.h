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
struct GPUInstanceManager;

struct RtScene
{
    RtScene(VulkanContext& ctx, ResourceManager& resources);
    ~RtScene();

    RtScene(const RtScene&) = delete;
    RtScene& operator=(const RtScene&) = delete;

    void updateBlases(VkCommandBuffer cmd, uint32_t frame, entt::registry& reg,
                      AssetManager& assets, const TerrainPass& terrain);
    void buildTlas(VkCommandBuffer cmd, uint32_t frame, entt::registry& reg,
                   GPUInstanceManager& instances, AssetManager& assets, const TerrainPass& terrain,
                   entt::entity camera);

    VkDescriptorSetLayout setLayout() const { return setLayout_; }
    VkDescriptorSet set(uint32_t frame) const { return sets_[frame]; }

   private:
    struct Blas
    {
        VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
        VkDeviceAddress address_ = 0;
        GPUResourceHandle buffer_;
        GPUResourceHandle source_;
        uint64_t size_ = 0;
        std::optional<uint32_t> query_;
        uint32_t querySlot_ = 0;
    };

    struct CompactionQueries
    {
        VkQueryPool pool_ = VK_NULL_HANDLE;
        uint32_t capacity_ = 0;
        uint32_t count_ = 0;
    };

    struct Tlas
    {
        VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
        GPUResourceHandle buffer_;
        GPUResourceHandle scratch_;
        GPUResourceHandle instances_;
        uint32_t capacity_ = 0;
    };

    struct HitTables
    {
        GPUResourceHandle geometries_;
        GPUResourceHandle instances_;
        uint32_t geometryCapacity_ = 0;
        uint32_t instanceCapacity_ = 0;
    };

    struct BlasInputs;
    VkAccelerationStructureKHR createAccelerationStructure(GPUResourceHandle buffer, uint64_t size,
                                                           VkAccelerationStructureTypeKHR type);
    Blas createBlas(uint64_t size);
    std::vector<Blas> buildBlases(VkCommandBuffer cmd, uint32_t frame, const BlasInputs& inputs);
    void compactBlases(VkCommandBuffer cmd, uint32_t frame);
    void growTlas(Tlas& tlas, uint32_t instances);
    void reserveHitTables(HitTables& tables, uint32_t geometries, uint32_t instances);

    VulkanContext& ctx_;
    ResourceManager& resources_;
    std::unordered_map<MeshHandle, Blas> meshBlases_;
    std::unordered_map<uint64_t, Blas> chunkBlases_;
    std::array<std::vector<Blas>, FramesInFlight> retired_;
    std::array<CompactionQueries, FramesInFlight> queries_;
    std::array<Tlas, FramesInFlight> tlases_;
    std::array<HitTables, FramesInFlight> hitTables_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, FramesInFlight> sets_{};
};
}  // namespace batap
