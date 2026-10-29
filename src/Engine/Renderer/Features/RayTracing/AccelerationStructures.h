#pragma once

#include <volk.h>

#include "Assets/AssetHandle.h"
#include "Handles.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/Graph/RenderGraph.h"

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
struct TerrainDrawer;
struct GPUInstanceManager;

struct AccelerationStructures
{
    AccelerationStructures(VulkanContext& ctx, ResourceManager& resources);
    ~AccelerationStructures();

    AccelerationStructures(const AccelerationStructures&) = delete;
    AccelerationStructures& operator=(const AccelerationStructures&) = delete;

    struct Buffers
    {
        GraphBuffer tlas_;
        GraphBuffer blases_;
    };

    Buffers addPasses(RenderGraph& graph, GraphBuffer uploads, GraphBuffer traced, uint32_t frame,
                      entt::registry& reg, GPUInstanceManager& instances, AssetManager& assets,
                      const TerrainDrawer& terrain, entt::entity camera);

    VkAccelerationStructureKHR tlas(uint32_t frame) const { return tlases_[frame].handle_; }
    GPUResourceHandle hitGeometries(uint32_t frame) const { return hitTables_[frame].geometries_; }
    GPUResourceHandle hitInstances(uint32_t frame) const { return hitTables_[frame].instances_; }

   private:
    struct Blas
    {
        VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
        VkDeviceAddress address_ = 0;
        GPUResourceHandle buffer_;
        GPUResourceHandle source_;
        uint64_t size_ = 0;
        uint64_t updateScratch_ = 0;
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
    void updateBlases(RenderGraph& graph, GraphBuffer blases, GraphBuffer uploads,
                      GraphBuffer traced, uint32_t frame, entt::registry& reg, AssetManager& assets,
                      const TerrainDrawer& terrain);
    void refitChunks(RenderGraph& graph, GraphBuffer blases, GraphBuffer traced,
                     const TerrainDrawer& terrain, const std::vector<uint64_t>& built);
    void addChunkGeometries(BlasInputs& inputs, const TerrainDrawer& terrain, uint64_t chunk) const;
    GraphBuffer buildTlas(RenderGraph& graph, GraphBuffer blases, uint32_t frame,
                          entt::registry& reg, GPUInstanceManager& instances, AssetManager& assets,
                          const TerrainDrawer& terrain, entt::entity camera);
    VkAccelerationStructureKHR createAccelerationStructure(GPUResourceHandle buffer, uint64_t size,
                                                           VkAccelerationStructureTypeKHR type);
    Blas createBlas(uint64_t size);
    std::vector<Blas> buildBlases(RenderGraph& graph, GraphBuffer blases, GraphBuffer uploads,
                                  GraphBuffer traced, uint32_t frame, BlasInputs inputs);
    void compactBlases(RenderGraph& graph, GraphBuffer blases, uint32_t frame);
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
};
}  // namespace batap
