#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{

struct FpsController_C
{
    float eyeHeight_ = 1.7f;
    float walkSpeed_ = 5.f;
    float mouseSensitivity_ = 0.0025f;

    // The heading is kept as a vector in the ground plane rather than an angle:
    // walking around a sphere turns the plane under the player, and a vector
    // carries over while a yaw measured from a world axis would not.
    v3f forward_ = {0.f, 0.f, -1.f};
    float pitch_ = 0.f;
};

static_assert(refl::fieldName<FpsController_C, 0>() == "eyeHeight");
static_assert(refl::fieldName<FpsController_C, 1>() == "walkSpeed");
static_assert(refl::fieldName<FpsController_C, 2>() == "mouseSensitivity");
static_assert(refl::fieldName<FpsController_C, 3>() == "forward");
static_assert(refl::fieldName<FpsController_C, 4>() == "pitch");

BATAP_COMPONENT(
    FpsController_C, "fpsController",
    fieldMeta<&FpsController_C::eyeHeight_>({.speed = 0.01f, .min = 0.f, .max = 10.f}),
    fieldMeta<&FpsController_C::walkSpeed_>({.speed = 0.1f, .min = 0.f, .max = 100.f}),
    fieldMeta<&FpsController_C::mouseSensitivity_>({.speed = 0.0001f, .min = 0.f, .max = 1.f}));

}  // namespace batap
