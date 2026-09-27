#pragma once

#include "EigenTypes.h"
#include "Renderer/EngineConfig.h"

#include <array>
#include <cstdint>

namespace batap
{
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
    // 0: the light is too close for this cascade, which is switched off.
    float radius_ = 0.f;
    float near_ = 0.f;
    float far_ = 0.f;
    float texelWorld_ = 0.f;
    // World to light rotation, rows x, y, then the direction to the light.
    m3f lightFrame_{m3f::Identity()};
};

struct ShadowFit
{
    std::array<CascadeSphere, ShadowCascadeCount> cascades_{};
    uint32_t count_ = 0;
};

ShadowFit fitShadowCascades(const ShadowFitInput& in);
}  // namespace batap
