#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
// A one-sided rectangle emitting along the -Z of its Transform_C, width_ along
// its X and height_ along its Y. Shaded exactly over its area by linearly
// transformed cosines: a surface sees its true solid angle, and a glossy one
// reflects its true shape.
struct RectLight_C
{
    col3 color_ = {1, 1, 1};
    // Radiance of the emitting face. Unlike a point light's, which scales what
    // a surface receives, it is what the face shows: the light a surface
    // receives follows the solid angle the rectangle covers from it.
    float intensity_ = 1;
    float width_ = 1;
    float height_ = 1;
    // Beyond this distance from the rectangle, nothing: a window over the
    // physical falloff, so that the shading loop can skip it.
    float radius_ = 10;
    // 0 keeps the physical falloff alone. Above, the light also fades as
    // (1 - d / radius_)^falloff_, d from the rectangle, like a point light's:
    // not physical, a reach to set by eye.
    float falloff_ = 0;
    // Full angle, in radians, around the normal, that the face emits within
    // along each of its axes: barn doors, or an egg-crate grid. pi, the
    // default, is the bare face. Narrower, the lit pool shrinks and its edge
    // sharpens, as wide as the face itself.
    float spreadAngle_ = 3.14159265f;
};

BATAP_COMPONENT(RectLight_C, "rectLight", ComponentMeta{.color = ComponentColor::Orange});
}  // namespace batap
