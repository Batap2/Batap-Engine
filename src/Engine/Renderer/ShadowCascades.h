#pragma once

#include "EigenTypes.h"
#include "Renderer/EngineConfig.h"
#include "Shaders/ShaderInterop.h"

#include <array>
#include <cstdint>

namespace batap
{
// Closer, a texel spans 5/3 across the sphere: reported as a scene error (R7).
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

// The camera only moves the window, by whole steps of a lattice fixed around
// the light; its axis and step change on coarse grids, so the texels hold
// still between changes.
struct CascadeSphere
{
    v3f center_{v3f::Zero()};
    // 0: the light is inside the sphere, and the cascade is switched off.
    float radius_ = 0.f;
    float near_ = 0.f;
    float far_ = 0.f;
    // Per unit distance from the light (texel = texelWorld_ * d); metres when
    // directional.
    float texelWorld_ = 0.f;
    float lightDistance_ = 0.f;  // in radii; infinite for a directional light
    bool directional_ = false;
    // Row z is the lattice axis, pointing at the light.
    m3f lightFrame_{m3f::Identity()};
    v3f lightPos_{v3f::Zero()};
    // On the gnomonic plane, in whole steps; in light-frame metres when
    // directional, depthNear_ then the higher z. Double: at a star's distance
    // a float step is longer than the sphere.
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

// Double: the light's distance enters every term, and the cancellation in
// float would lose the sphere.
m4d cascadeViewProjExact(const CascadeSphere& s);
m4f cascadeViewProj(const CascadeSphere& s);
}  // namespace batap
