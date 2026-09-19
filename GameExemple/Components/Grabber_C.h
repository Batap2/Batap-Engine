#pragma once

#include "Reflection/ComponentRegistry.h"

#include <entt/entt.hpp>

namespace batap
{

struct Grabber_C
{
    float reach_ = 3.f;
    float holdDistance_ = 2.f;

    // A spring in N/m, not an acceleration: the sag is then mass * g / stiffness,
    // so a heavy crate visibly hangs lower and swings slower than a light one.
    float stiffness_ = 1200.f;
    float damping_ = 150.f;
    float maxForce_ = 4000.f;

    entt::entity held_ = entt::null;
};

static_assert(refl::fieldName<Grabber_C, 0>() == "reach");
static_assert(refl::fieldName<Grabber_C, 1>() == "holdDistance");
static_assert(refl::fieldName<Grabber_C, 2>() == "stiffness");
static_assert(refl::fieldName<Grabber_C, 3>() == "damping");
static_assert(refl::fieldName<Grabber_C, 4>() == "maxForce");

BATAP_COMPONENT(Grabber_C, "grabber", fieldSkip<&Grabber_C::held_>(),
                fieldMeta<&Grabber_C::reach_>({.speed = 0.05f, .min = 0.f, .max = 50.f}),
                fieldMeta<&Grabber_C::holdDistance_>({.speed = 0.05f, .min = 0.f, .max = 50.f}),
                fieldMeta<&Grabber_C::stiffness_>({.speed = 10.f, .min = 0.f, .max = 1e6f}),
                fieldMeta<&Grabber_C::damping_>({.speed = 1.f, .min = 0.f, .max = 1e5f}),
                fieldMeta<&Grabber_C::maxForce_>({.speed = 10.f, .min = 0.f, .max = 1e6f}));

}  // namespace batap
