#pragma once

#include "Reflection/ComponentRegistry.h"

namespace batap
{

struct OrbitCam_C
{
    float distance_ = 30.f;
    float minDistance_ = 2.f;
    float maxDistance_ = 500.f;
    float zoomStep_ = 1.15f;
    float mouseSensitivity_ = 0.0025f;

    float yaw_ = 0.f;
    float pitch_ = -0.3f;
};

static_assert(refl::fieldName<OrbitCam_C, 0>() == "distance");
static_assert(refl::fieldName<OrbitCam_C, 1>() == "minDistance");
static_assert(refl::fieldName<OrbitCam_C, 2>() == "maxDistance");
static_assert(refl::fieldName<OrbitCam_C, 3>() == "zoomStep");
static_assert(refl::fieldName<OrbitCam_C, 4>() == "mouseSensitivity");
static_assert(refl::fieldName<OrbitCam_C, 5>() == "yaw");
static_assert(refl::fieldName<OrbitCam_C, 6>() == "pitch");

BATAP_COMPONENT(OrbitCam_C, "orbitCam",
                fieldMeta<&OrbitCam_C::distance_>({.speed = 0.1f, .min = 0.01f, .max = 1e5f}),
                fieldMeta<&OrbitCam_C::minDistance_>({.speed = 0.1f, .min = 0.01f, .max = 1e5f}),
                fieldMeta<&OrbitCam_C::maxDistance_>({.speed = 1.f, .min = 0.01f, .max = 1e5f}),
                fieldMeta<&OrbitCam_C::zoomStep_>({.speed = 0.01f, .min = 1.01f, .max = 3.f}),
                fieldMeta<&OrbitCam_C::mouseSensitivity_>({.speed = 0.0001f, .min = 0.f, .max = 1.f}));

}  // namespace batap
