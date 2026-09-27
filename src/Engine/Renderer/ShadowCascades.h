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
    uint32_t count_ = ShadowCascadeCount;
};

struct CascadeSphere
{
    v3f center_{v3f::Zero()};
    float radius_ = 0.f;
    float near_ = 0.f;
    float far_ = 0.f;
    float texelWorld_ = 0.f;
};

struct ShadowFit
{
    std::array<CascadeSphere, ShadowCascadeCount> cascades_{};
    uint32_t count_ = 0;
};

ShadowFit fitShadowCascades(const ShadowFitInput& in);
}  // namespace batap
