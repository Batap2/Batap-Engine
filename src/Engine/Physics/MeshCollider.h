#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "Serialization/BmeshSerializer.h"

#include <cstdint>
#include <vector>

namespace batap
{

// Null when nothing usable is left once Jolt has dropped the degenerate
// triangles. Winding is kept as authored: the front face is the one the
// renderer draws.
JPH::ShapeRefC makeMeshShape(const BmeshData& data);

// Over a grid of cells + 1 vertices a side, row-major, wound like a terrain
// chunk.
JPH::ShapeRefC makeGridMeshShape(const std::vector<v3f>& positions, int32_t cells);

}  // namespace batap
