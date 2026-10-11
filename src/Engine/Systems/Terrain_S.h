#pragma once

#include "EigenTypes.h"
#include "Terrain/TerrainChunk.h"

#include <entt/entt.hpp>

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

   private:
    struct Chunk
    {
        uint64_t mesh_ = 0;
        v3d center_ = v3d::Zero();
    };

    // Per entity with a Terrain_C.
    struct State
    {
        uint64_t fingerprint_ = 0;
        std::unordered_map<ChunkKey, Chunk, ChunkKeyHash> chunks_;
    };

    void drop(World& world, State& state);

    std::unordered_map<entt::entity, State> states_;
};

}  // namespace batap
