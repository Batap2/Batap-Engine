#pragma once

#include "EigenTypes.h"
#include "Renderer/EngineConfig.h"
#include "Shaders/ShaderInterop.h"

#include <array>
#include <cstdint>

namespace batap
{
// Closer than this, one direction no longer serves the whole sphere and the
// shadows skew: a scene error, reported (Objectif_Ombres R7).
constexpr float CascadeLightMinRadii = 4.f;

struct ShadowFitInput
{
    v3f camPos_{v3f::Zero()};
    v3f camFwd_{-v3f::UnitZ()};
    float fovY_ = 1.f;
    float aspect_ = 1.f;
    float range_ = 0.f;
    v3f lightPos_{v3f::Zero()};
    uint32_t count_ = ShadowCascadeCount;
};

struct CascadeSphere
{
    // Snapped to the texel grid of lightFrame_, so it may sit up to half a
    // texel per axis off the slice it bounds.
    v3f center_{v3f::Zero()};
    // 0: the light is inside the sphere, and the cascade is switched off.
    float radius_ = 0.f;
    float near_ = 0.f;
    float far_ = 0.f;
    float texelWorld_ = 0.f;
    float lightDistance_ = 0.f;  // in radii
    // World to light rotation, rows x, y, then the direction to the light.
    m3f lightFrame_{m3f::Identity()};
};

struct ShadowFit
{
    std::array<CascadeSphere, ShadowCascadeCount> cascades_{};
    uint32_t count_ = 0;
};

ShadowFit fitShadowCascades(const ShadowFitInput& in);

m4f cascadeViewProj(const CascadeSphere& s);
}  // namespace batap
