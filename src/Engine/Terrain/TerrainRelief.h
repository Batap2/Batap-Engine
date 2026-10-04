#pragma once

#include "EigenTypes.h"

#include <entt/entt.hpp>

#include <array>
#include <cstdint>

namespace batap
{
struct World;

// The game's ground: how high each grid point rises, and which material each
// triangle gets. Must answer the same for the same point, or seams crack.
struct TerrainRelief
{
    struct Query
    {
        // The point on the bare sphere or plane, and its unit vertical.
        v3d base_ = v3d::Zero();
        v3d up_ = v3d::UnitY();
        int32_t level_ = 0;
        // Metres between two vertices at this level: finer detail cannot show.
        double cell_ = 0.0;
    };

    struct Sample
    {
        // Metres along up_.
        double height_ = 0.0;
        // Free for the game; handed back to stratum() for each triangle.
        int32_t zone_ = 0;
    };

    virtual ~TerrainRelief() = default;

    virtual Sample sample(const Query& q) const = 0;
    // The material of a triangle, as an index into the entity's Materials_C
    // slots. slope: 1 for flat ground, 0 for a vertical wall.
    virtual uint8_t stratum(double slope, const std::array<int32_t, 3>& zones) const = 0;
};

// Gives the relief of each entity with a Terrain_C. Passed to Terrain_S::sync()
// every frame; the engine keeps no pointer to it.
struct TerrainSource
{
    struct Ground
    {
        // Null: no terrain on this entity.
        const TerrainRelief* relief_ = nullptr;
        // The sphere's radius or the plane's side, in metres.
        double size_ = 0.0;
        // Every chunk is rebuilt when it changes, none while it stays.
        uint64_t settings_ = 0;
    };

    virtual ~TerrainSource() = default;

    // Called every frame: keep it cheap. relief_ must live until the next call.
    virtual Ground ground(World& world, entt::entity entity, int32_t cells) = 0;
};

}  // namespace batap
