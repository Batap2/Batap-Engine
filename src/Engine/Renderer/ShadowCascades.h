#pragma once

#include "EigenTypes.h"
#include "Renderer/EngineConfig.h"
#include "Shaders/ShaderInterop.h"

#include <array>
#include <cstdint>

namespace batap
{
// Closer than this, a texel spans (D + r) / (D - r) = 5/3 across the sphere
// and the cone opens to 29 degrees: a scene error, reported (Objectif_Ombres
// R7).
constexpr float CascadeLightMinRadii = 4.f;

struct ShadowFitInput
{
    v3f camPos_{v3f::Zero()};
    v3f camFwd_{-v3f::UnitZ()};
    float fovY_ = 1.f;
    float aspect_ = 1.f;
    float range_ = 0.f;
    v3f lightPos_{v3f::Zero()};
    v3f lightDir_{v3f::Zero()};
    uint32_t count_ = ShadowCascadeCount;
};

// A cascade is a perspective view from the light, aimed at the slice's sphere:
// exact for a positional light, so which points are shadowed never depends on
// the camera. What the camera moves is the window only, by whole steps of a
// lattice fixed around the light. The lattice's axis and its step change on
// coarse grids, so the rasterisation holds still between two changes, and a
// change re-lays the texels without moving the shadow.
//
// Under a directional light, the view is orthographic along one fixed
// direction, and the window moves by whole texels on all three axes of the
// light's frame: the grid is pinned to the world, and nothing ever re-lays it.
struct CascadeSphere
{
    v3f center_{v3f::Zero()};
    // 0: the light is inside the sphere, and the cascade is switched off.
    float radius_ = 0.f;
    float near_ = 0.f;
    float far_ = 0.f;
    // Step of the lattice on the gnomonic plane at unit distance from the
    // light: a texel is texelWorld_ * d wide at distance d from it. Under a
    // directional light, the texel itself, in metres.
    float texelWorld_ = 0.f;
    float lightDistance_ = 0.f;  // in radii; infinite for a directional light
    bool directional_ = false;
    // World to light rotation, rows x, y, then the lattice axis pointing at
    // the light: the view looks down -z, from the light.
    m3f lightFrame_{m3f::Identity()};
    v3f lightPos_{v3f::Zero()};
    // The window's centre on the gnomonic plane, a whole number of steps, and
    // the depth range along the axis, from the light. Double: at a star's
    // distance a float step is longer than the sphere. Under a directional
    // light, the window's centre in the light's frame, in metres, and the
    // depth range as z in that frame: depthNear_ the higher, toward the light.
    v2d window_{v2d::Zero()};
    double depthNear_ = 0.0;
    double depthFar_ = 0.0;
};

struct ShadowFit
{
    std::array<CascadeSphere, ShadowCascadeCount> cascades_{};
    uint32_t count_ = 0;
};

ShadowFit fitShadowCascades(const ShadowFitInput& in);

// The view-projection the pass renders with and the shader reads, in double:
// the light's distance enters every term, and the cancellation in float would
// lose the sphere. Rounded once, for the GPU, by cascadeViewProj.
m4d cascadeViewProjExact(const CascadeSphere& s);
m4f cascadeViewProj(const CascadeSphere& s);
}  // namespace batap
