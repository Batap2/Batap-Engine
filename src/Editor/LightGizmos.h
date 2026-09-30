#pragma once

#include "EigenTypes.h"

namespace batap
{
struct World;
struct Selection;

// A fixed share of the view, wherever the light's transform sits.
float sunArrowLength(World& world, const v3f& pos);

// Shape of each selected light: a point light's range, a spot's cones, a rect
// light's face and the volume its barn doors let through, a directional
// light's direction.
void drawLightGizmos(World& world, const Selection& selection);
}  // namespace batap
