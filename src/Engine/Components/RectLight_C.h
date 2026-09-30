#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
// One-sided, emits along -Z of its Transform_C; width_ along X, height_ along Y.
struct RectLight_C
{
    col3 color_ = {1, 1, 1};
    // Radiance of the face, not a point light's received intensity.
    float intensity_ = 1;
    float width_ = 1;
    float height_ = 1;
    // Hard cutoff distance from the rectangle.
    float radius_ = 10;
    // 0: physical falloff only. Above, also fades as (1 - d / radius_)^falloff_.
    float falloff_ = 0;
    // Full emission angle around the normal, per axis, in radians; pi = bare face.
    float spreadAngle_ = 3.14159265f;
    // Hard, from one point of the face; only analytic occluders soften.
    bool castShadows_ = false;
};

BATAP_COMPONENT(RectLight_C, "rectLight", ComponentMeta{.color = ComponentColor::Orange});
}  // namespace batap
