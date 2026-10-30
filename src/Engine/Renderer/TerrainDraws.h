#pragma once

#include "EigenTypes.h"
#include "Shaders/ShaderInterop.h"
#include "Terrain/TerrainChunk.h"

#include <array>
#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

namespace batap
{

// From Terrain_S to the terrain pass, as Billboards. A mesh lives on the GPU
// from its add() to its remove(), is drawn while shown, and must be placed
// every frame it is.
struct TerrainDraws
{
    // TerrainChunkGPUData's fields that change while the mesh stays.
    struct Params
    {
        std::array<uint32_t, kMaxTerrainStrata> materials_{};
        uint32_t force_ = 0;
        float morphStart_ = 0.f;
        float morphEnd_ = 0.f;
        bool morphs_ = false;
        bool outline_ = false;

        // Bit for bit: any change is one to send.
        bool operator==(const Params& o) const
        {
            return materials_ == o.materials_ && force_ == o.force_ &&
                   std::bit_cast<uint32_t>(morphStart_) == std::bit_cast<uint32_t>(o.morphStart_) &&
                   std::bit_cast<uint32_t>(morphEnd_) == std::bit_cast<uint32_t>(o.morphEnd_) &&
                   morphs_ == o.morphs_ && outline_ == o.outline_;
        }
    };

    struct Mesh
    {
        std::vector<TerrainVertexGPUData> vertices_;
        std::vector<uint16_t> indices_;
        std::array<StratumRange, kMaxTerrainStrata> ranges_{};
        uint32_t cells_ = 0;
        uint32_t level_ = 0;
        Params params_;
        bool shown_ = false;
    };

    struct Placement
    {
        m4f world_;
        v3d centreInOwner_;
        v3d cameraInOwner_;
        uint32_t owner_ = 0;
    };

    // Uploaded with the next frame.
    uint64_t add(Mesh mesh);
    void remove(uint64_t mesh);
    void setParams(uint64_t mesh, const Params& params);
    void show(uint64_t mesh, bool shown);
    // This frame's matrix: chunk space to world space less the render
    // camera's position.
    void place(uint64_t mesh, const Placement& placement);

    // Read by the renderer, then emptied.
    std::vector<std::pair<uint64_t, Mesh>> added_;
    std::vector<uint64_t> removed_;
    std::vector<std::pair<uint64_t, Params>> paramChanges_;
    std::vector<std::pair<uint64_t, bool>> shownChanges_;
    std::vector<std::pair<uint64_t, Placement>> placed_;
    uint64_t next_ = 1;

    // Written back by the renderer, for the debug view.
    struct Upload
    {
        size_t meshes_ = 0;
        uint64_t bytes_ = 0;
        double ms_ = 0.0;
    };
    Upload lastUpload_;
};

}  // namespace batap
