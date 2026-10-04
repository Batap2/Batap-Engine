#pragma once

#include "EigenTypes.h"
#include "Terrain/TerrainLod.h"

#include <entt/entt.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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
    // After the frame's last transform update, or a chunk lags its astre by
    // a frame.
    void publish(World& world);
    void clear(World& world);

   private:
    struct Chunk
    {
        uint64_t mesh_ = 0;
        v3d center_ = v3d::Zero();
        uint32_t force_ = 0;
        std::array<BorderPoint, 16> border_{};
    };

    // Per astre. The node boxes live for as long as the geometry settings do.
    struct State
    {
        uint64_t settings_ = 0;
        NodeBoundsCache bounds_;
        std::unordered_map<ChunkKey, Chunk, ChunkKeyHash> chunks_;
        TerrainGrid grid_;
        double ratio_ = 0.0;
        int32_t maxLevel_ = 0;
        bool morphs_ = false;

        size_t built_ = 0;
        size_t dropped_ = 0;
        size_t totalBuilt_ = 0;
        double lastBuildMs_ = 0.0;
    };

    void drop(World& world, State& state);
    void debug(World& world, entt::entity astre, const State& state, const transform& astreWorld,
               const std::optional<v3d>& camera);

    std::unordered_map<entt::entity, State> states_;
};

}  // namespace batap
