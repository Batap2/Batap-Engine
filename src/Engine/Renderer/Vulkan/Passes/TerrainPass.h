#pragma once

#include "Handles.h"
#include "Renderer/Vulkan/Passes/Pass.h"
#include "Renderer/Vulkan/Passes/ShaderCatalog.h"
#include "Terrain/TerrainChunk.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace batap
{
struct TerrainDraws;

struct TerrainPass
{
    explicit TerrainPass(const PassSetup& setup);
    ~TerrainPass();

    TerrainPass(const TerrainPass&) = delete;
    TerrainPass& operator=(const TerrainPass&) = delete;

    void buildPipelines(const ShaderModules& modules);
    // Same contract as the instance pools: staged during the update, copied
    // at the top of the next render.
    void upload(TerrainDraws& draws);
    void record(const PassContext& pass);

    GPUResourceHandle chunkBuffer() const { return chunks_; }
    GPUResourceHandle vertexBuffer() const { return vertices_.buffer_; }

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

    struct Slot
    {
        uint64_t vertex_ = 0;
        uint64_t index_ = 0;
        uint64_t vertices_ = 0;
        uint64_t indices_ = 0;
        std::array<StratumRange, kMaxTerrainStrata> ranges_{};
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

    PassSetup setup_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    GPUResourceHandle chunks_;
    Pool vertices_;
    Pool indices_;
    std::unordered_map<uint64_t, Slot> slots_;
    // Stamped with the frame they left: frames in flight may still read them.
    std::vector<std::pair<uint64_t, Slot>> retiredSlots_;
    std::vector<std::pair<uint64_t, GPUResourceHandle>> retiredBuffers_;
    uint64_t frame_ = 0;
    std::vector<TerrainChunkGPUData> instances_;
    std::vector<Draw> draws_;
    bool warnedFull_ = false;
};
}  // namespace batap
