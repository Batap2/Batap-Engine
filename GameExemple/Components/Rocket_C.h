#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{

struct Rocket_C
{
    float maxThrust_ = 12000.f;
    float burnRate_ = 8.f;
    float fuel_ = 400.f;
    float dryMass_ = 600.f;

    // Local, where the thrust pushes. Off the centre of mass is the whole
    // point: that is what turns a badly loaded hold into a torque.
    v3f enginePos_ = {0.f, -4.f, 0.f};

    float rcsTorque_ = 8000.f;
    bool assist_ = true;
    float assistGain_ = 4000.f;

    // Written by the game every fixed tick.
    float throttle_ = 0.f;
    v3f rcsInput_ = v3f::Zero();
};

static_assert(refl::fieldName<Rocket_C, 0>() == "maxThrust");
static_assert(refl::fieldName<Rocket_C, 1>() == "burnRate");
static_assert(refl::fieldName<Rocket_C, 2>() == "fuel");
static_assert(refl::fieldName<Rocket_C, 3>() == "dryMass");
static_assert(refl::fieldName<Rocket_C, 4>() == "enginePos");
static_assert(refl::fieldName<Rocket_C, 5>() == "rcsTorque");
static_assert(refl::fieldName<Rocket_C, 6>() == "assist");
static_assert(refl::fieldName<Rocket_C, 7>() == "assistGain");

BATAP_COMPONENT(Rocket_C, "rocket",
                fieldMeta<&Rocket_C::maxThrust_>({.speed = 50.f, .min = 0.f, .max = 1e7f}),
                fieldMeta<&Rocket_C::burnRate_>({.speed = 0.1f, .min = 0.f, .max = 1000.f}),
                fieldMeta<&Rocket_C::fuel_>({.speed = 1.f, .min = 0.f, .max = 1e6f}),
                fieldMeta<&Rocket_C::dryMass_>({.speed = 1.f, .min = 0.1f, .max = 1e6f}),
                fieldMeta<&Rocket_C::rcsTorque_>({.speed = 50.f, .min = 0.f, .max = 1e7f}),
                fieldMeta<&Rocket_C::assistGain_>({.speed = 50.f, .min = 0.f, .max = 1e7f}),
                fieldMeta<&Rocket_C::throttle_>({.speed = 0.01f, .min = 0.f, .max = 1.f}));

}  // namespace batap
