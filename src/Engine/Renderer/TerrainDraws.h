#pragma once

#include "EigenTypes.h"
#include "Shaders/ShaderInterop.h"
#include "Terrain/TerrainChunk.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace batap
{

// From Terrain_S to the terrain pass, as Billboards.
struct TerrainDraws
{
    struct Mesh
    {
        uint32_t cells_ = 0;
        std::vector<TerrainVertexGPUData> vertices_;
        std::vector<uint16_t> indices_;
        std::array<StratumRange, kMaxTerrainStrata> ranges_{};
    };

    struct Chunk
    {
        uint64_t mesh_ = 0;
        m4f world_ = m4f::Identity();
        std::array<uint32_t, kMaxTerrainStrata> materials_{};
        float morphStart_ = 0.f;
        float morphEnd_ = 0.f;
        uint32_t force_ = 0;
        uint32_t flags_ = 0;
    };

    // Uploaded with the next frame.
    uint64_t add(Mesh mesh);
    void remove(uint64_t mesh);

    // Shown this frame; emptied once the renderer has read it.
    std::vector<Chunk> chunks_;

    std::vector<std::pair<uint64_t, Mesh>> added_;
    std::vector<uint64_t> removed_;
    uint64_t next_ = 1;
    // Added and not removed, every astre together.
    size_t live_ = 0;
};

}  // namespace batap
