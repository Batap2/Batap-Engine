#pragma once

#include "EigenTypes.h"
#include "Renderer/TerrainDraws.h"
#include "Terrain/ChunkBuilder.h"
#include "Terrain/TerrainLod.h"

#include <entt/entt.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace batap
{
struct World;
struct TerrainSource;

struct Terrain_S
{
    Terrain_S();

    // The source comes with every call: nothing here keeps a pointer into the
    // game module, which a hot reload unloads.
    void sync(World& world, TerrainSource& source);
    // After the frame's last transform update, or a chunk lags its entity by
    // a frame.
    void publish(World& world);
    // Waits for the builds under way, which run game code.
    void clear(World& world);

    // Chunk outlines coloured by level, and a count on screen.
    bool showChunks_ = false;

   private:
    struct Chunk
    {
        uint64_t mesh_ = 0;
        v3d center_ = v3d::Zero();
        uint32_t force_ = 0;
        bool shown_ = false;
        // The last sync it was in the quadtree: the cache drops the oldest.
        uint64_t used_ = 0;
        // As last sent to TerrainDraws.
        TerrainDraws::Params params_;
    };

    struct Wish
    {
        double distance_ = 0.0;
        ChunkBuilder::Request request_;
    };

    // Per entity with a Terrain_C. The node boxes live for as long as the
    // fingerprint does.
    struct State
    {
        uint64_t fingerprint_ = 0;
        NodeBoundsCache bounds_;
        // On the GPU, shown or not.
        std::unordered_map<ChunkKey, Chunk, ChunkKeyHash> chunks_;
        std::vector<ChunkKey> wanted_;
        ChunkKeySet shown_;
        std::vector<Wish> wishes_;
        // For the debug view.
        TerrainGrid grid_;
        double ratio_ = 0.0;
        int32_t maxLevel_ = 0;
        size_t arrived_ = 0;
        size_t evicted_ = 0;
        size_t totalBuilt_ = 0;
        double syncMs_ = 0.0;
        std::chrono::steady_clock::time_point rateStart_;
        size_t rateBuilt_ = 0;
        double rateBuildMs_ = 0.0;
        double chunksPerSecond_ = 0.0;
        double msPerChunk_ = 0.0;
    };

    void drop(World& world, State& state);
    void debug(World& world, entt::entity owner, const State& state);

    std::unordered_map<entt::entity, State> states_;
    uint64_t syncs_ = 0;
    ChunkBuilder builder_;
};

}  // namespace batap
