#pragma once

#include "Components/Camera_C.h"
#include "Components/FreeCamController_C.h"
#include "EigenTypes.h"

#include <algorithm>
#include <cmath>

namespace batap
{

struct Engine;
struct World;

inline constexpr float kFreeCamPitchLimit = 1.55334306f;

inline quatf freeCamRotation(const FreeCamController_C& c)
{
    return (c.frame_ * quatf{angleaxisf(c.yaw_, v3f::UnitY())} *
            quatf{angleaxisf(c.pitch_, v3f::UnitX())})
        .normalized();
}

// Takes `frame` while still looking where `current` looks: only the horizon
// turns.
inline void setFreeCamFrame(FreeCamController_C& c, const quatf& frame, const quatf& current)
{
    c.frame_ = frame.normalized();
    const v3f look = c.frame_.conjugate() * (current * -v3f::UnitZ());
    c.yaw_ = std::atan2(-look.x(), -look.z());
    c.pitch_ = std::clamp(std::asin(std::clamp(look.y(), -1.f, 1.f)), -kFreeCamPitchLimit,
                          kFreeCamPitchLimit);
}

inline constexpr float kFreeCamOrthoMinHeight = 0.001f;

inline void enterFreeCamOrtho(FreeCamController_C& c, Camera_C& cam, float focus)
{
    c.orthoFocus_ = focus;
    c.orthoLook_ = 0.f;
    cam.orthoHeight_ = std::max(2.f * focus * std::tan(cam.fov_ * 0.5f), kFreeCamOrthoMinHeight);
}

inline void leaveFreeCamOrtho(EntityHandle h, FreeCamController_C& c, Camera_C& cam)
{
    const float framing = cam.orthoHeight_ * 0.5f / std::tan(cam.fov_ * 0.5f);
    h.translate(v3f::UnitZ() * (framing - c.orthoFocus_), Space::Local);
    cam.orthoHeight_ = 0.f;
    c.orthoLook_ = 0.f;
}

struct FreeCamController_S
{
    void update(Engine& ctx, World& world, float deltaTime);
};
}  // namespace batap
