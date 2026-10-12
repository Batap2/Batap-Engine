#pragma once

#include "Reflection/ComponentRegistry.h"
#include "Terrain/TerrainShape.h"

#include <cstdint>

namespace batap
{

// The quadtree follows the render camera down to finestCell_; without a
// camera, the roots show. With oneChunk_, the one node named by face_,
// level_, x_ and y_ shows instead.
struct Terrain_C
{
    int32_t level_ = 0;
    bool oneChunk_ = false;
    int32_t face_ = 2;
    int32_t x_ = 0;
    int32_t y_ = 0;
    // Triangles per chunk side; every level halves the cell. Powers of two
    // from 16 to 128, rounded down otherwise.
    int32_t cells_ = 32;
    // Metres. Sets the deepest level of the quadtree.
    float finestCell_ = 0.01f;
    // A node splits while the camera is closer than this many times its side.
    float lodRatio_ = 2.f;
    // 0 a sphere, 1 a square plane centred on the entity, up +Y. Its radius
    // or side comes from the TerrainSource.
    TerrainShape shape_ = TerrainShape::Sphere;
};

static_assert(refl::fieldName<Terrain_C, 0>() == "level");
static_assert(refl::fieldName<Terrain_C, 1>() == "oneChunk");
static_assert(refl::fieldName<Terrain_C, 2>() == "face");
static_assert(refl::fieldName<Terrain_C, 3>() == "x");
static_assert(refl::fieldName<Terrain_C, 4>() == "y");
static_assert(refl::fieldName<Terrain_C, 5>() == "cells");
static_assert(refl::fieldName<Terrain_C, 6>() == "finestCell");
static_assert(refl::fieldName<Terrain_C, 7>() == "lodRatio");
static_assert(refl::fieldName<Terrain_C, 8>() == "shape");

BATAP_COMPONENT(Terrain_C, "terrain",
                fieldMeta<&Terrain_C::level_>({.speed = 1.f, .min = 0.f, .max = 24.f}),
                fieldMeta<&Terrain_C::face_>({.speed = 1.f, .min = 0.f, .max = 5.f}),
                fieldMeta<&Terrain_C::x_>({.speed = 1.f, .min = 0.f, .max = 16777215.f}),
                fieldMeta<&Terrain_C::y_>({.speed = 1.f, .min = 0.f, .max = 16777215.f}),
                fieldMeta<&Terrain_C::cells_>({.speed = 16.f, .min = 16.f, .max = 128.f}),
                fieldMeta<&Terrain_C::finestCell_>({.speed = 0.001f, .min = 0.001f, .max = 1000.f}),
                fieldMeta<&Terrain_C::lodRatio_>({.speed = 0.1f, .min = 1.f, .max = 8.f}),
                fieldMeta<&Terrain_C::shape_>({.speed = 1.f, .min = 0.f, .max = 1.f}));

}  // namespace batap
