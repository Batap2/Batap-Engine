#pragma once

#include "EigenTypes.h"

#include <array>
#include <cstdint>

namespace batap
{

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

}  // namespace batap
