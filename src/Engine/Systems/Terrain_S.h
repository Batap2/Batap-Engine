#pragma once

#include "EigenTypes.h"
#include "Renderer/TerrainDraws.h"
#include "Terrain/TerrainLod.h"

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace batap
{
struct World;
struct TerrainSource;

struct Terrain_S
{
    // The source comes with every call: nothing here keeps a pointer into the
    // game module, which a hot reload unloads.
    void sync(World& world, TerrainSource& source);
    // After the frame's last transform update, or a chunk lags its entity by
    // a frame.
    void publish(World& world);
    void clear(World& world);

    // Chunk outlines coloured by level, and a count on screen.
    bool showChunks_ = false;

   private:
    struct Chunk
    {
        uint64_t mesh_ = 0;
        v3d center_ = v3d::Zero();
        uint32_t force_ = 0;
        // As last sent to TerrainDraws.
        TerrainDraws::Params params_;
    };

    // Per entity with a Terrain_C. The node boxes live for as long as the
    // fingerprint does.
    struct State
    {
        uint64_t fingerprint_ = 0;
        NodeBoundsCache bounds_;
        std::unordered_map<ChunkKey, Chunk, ChunkKeyHash> chunks_;
        // For the debug view.
        TerrainGrid grid_;
        double ratio_ = 0.0;
        int32_t maxLevel_ = 0;
        size_t built_ = 0;
        size_t dropped_ = 0;
        size_t totalBuilt_ = 0;
        double lastBuildMs_ = 0.0;
    };

    void drop(World& world, State& state);
    void debug(World& world, entt::entity owner, const State& state);

    std::unordered_map<entt::entity, State> states_;
};

}  // namespace batap
