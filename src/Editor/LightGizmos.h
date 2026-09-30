#pragma once

#include "EigenTypes.h"

namespace batap
{
struct World;
struct Selection;

float sunArrowLength(World& world, const v3f& pos);

void drawLightGizmos(World& world, const Selection& selection);
}  // namespace batap
