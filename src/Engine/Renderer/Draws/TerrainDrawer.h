#pragma once

#include "Handles.h"
#include "Renderer/Device/ShaderLibrary.h"
#include "Renderer/Draws/DrawContext.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/TerrainDraws.h"
#include "Shaders/ShaderInterop.h"
#include "Terrain/TerrainChunk.h"

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace batap
{

struct TerrainDrawer
{
    explicit TerrainDrawer(const DrawSetup& setup);
    ~TerrainDrawer();

    TerrainDrawer(const TerrainDrawer&) = delete;
    TerrainDrawer& operator=(const TerrainDrawer&) = delete;

    void buildPipelines(const ShaderModules& modules);
    // Same contract as the instance pools: staged during the update, copied
    // at the top of the next render.
    void upload(TerrainDraws& draws, bool tracing);
    GraphBuffer addMorphPass(RenderGraph& graph, const DrawContext& draw, GraphBuffer uploads);
    void recordPrepass(const DrawContext& draw);
    void record(const DrawContext& draw);
    // Inside a shadow view's rendering, its push constants set.
    void recordShadow(const DrawContext& draw);

    struct Slot
    {
        uint64_t vertex_ = 0;
        uint64_t index_ = 0;
        uint64_t traced_ = 0;
        uint64_t vertices_ = 0;
        uint64_t indices_ = 0;
        // Into chunks_ and worlds_.
        uint32_t instance_ = 0;
        uint32_t cells_ = 0;
        uint32_t level_ = 0;
        std::array<StratumRange, kMaxTerrainStrata> ranges_{};
        bool shown_ = false;
        bool morphs_ = false;
        bool tracedReady_ = false;
        m4f placedWorld_ = m4f::Identity();
        uint64_t placedFrame_ = 0;
        v3d placedCamera_ = v3d::Zero();
        std::optional<v3d> morphedCamera_;
        double morphWidth_ = 0.0;
    };

    GPUResourceHandle chunkBuffer() const { return chunks_; }
    GPUResourceHandle vertexBuffer() const { return vertices_.buffer_; }
    GPUResourceHandle indexBuffer() const { return indices_.buffer_; }
    GPUResourceHandle worldBuffer() const { return worlds_; }
    GPUResourceHandle tracedBuffer() const { return traced_.buffer_; }
    GPUResourceHandle morphRowBuffer() const { return morphRows_; }
    const std::unordered_map<uint64_t, Slot>& slots() const { return slots_; }
    const std::vector<TerrainWorldGPUData>& worlds() const { return worldData_; }
    const std::vector<uint64_t>& morphed() const { return morphed_; }

   private:
    // In elements, vertices or indices.
    struct Pool
    {
        GPUResourceHandle buffer_;
        uint64_t stride_ = 0;
        uint64_t capacity_ = 0;
        uint64_t end_ = 0;
        std::unordered_map<uint64_t, std::vector<uint64_t>> free_;
    };

    struct Draw
    {
        uint32_t instance_ = 0;
        uint32_t stratum_ = 0;
        uint32_t firstIndex_ = 0;
        uint32_t count_ = 0;
    };

    void reserve(Pool& pool, uint64_t elements);
    uint64_t allocate(Pool& pool, uint64_t elements);
    std::optional<uint32_t> allocateInstance();
    void recordDraws(const DrawContext& draw, VkPipeline pipeline, const std::vector<Draw>& draws);
    void writeChunk(uint32_t instance, const Slot& slot, const TerrainDraws::Params& params);
    void planMorphs(bool tracing);

    DrawSetup setup_;
    VkPipeline prepassPipeline_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline rtPipeline_ = VK_NULL_HANDLE;
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipeline morphPipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle chunks_;
    GPUResourceHandle worlds_;
    GPUResourceHandle morphRows_;
    Pool vertices_;
    Pool indices_;
    Pool traced_;
    std::unordered_set<uint64_t> toMorph_;
    bool tracing_ = false;
    std::vector<uint64_t> morphed_;
    std::vector<TerrainMorphRowGPUData> morphRowData_;
    std::unordered_map<uint64_t, Slot> slots_;
    std::vector<uint32_t> freeInstances_;
    uint32_t instanceEnd_ = 0;
    // Stamped with the frame they left: frames in flight may still read them.
    std::vector<std::pair<uint64_t, Slot>> retiredSlots_;
    std::vector<std::pair<uint64_t, uint32_t>> retiredInstances_;
    std::vector<std::pair<uint64_t, GPUResourceHandle>> retiredBuffers_;
    uint64_t frame_ = 0;
    std::vector<TerrainWorldGPUData> worldData_;
    std::vector<Draw> draws_;
    std::vector<Draw> chunkDraws_;
    bool drawsDirty_ = false;
    uint64_t stagedBytes_ = 0;
    bool warnedFull_ = false;
};
}  // namespace batap
