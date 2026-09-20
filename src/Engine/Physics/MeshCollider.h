#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "Serialization/BmeshSerializer.h"

namespace batap
{

// Null when nothing usable is left once Jolt has dropped the degenerate
// triangles. Winding is kept as authored: the front face is the one the
// renderer draws.
JPH::ShapeRefC makeMeshShape(const BmeshData& data);

}  // namespace batap
