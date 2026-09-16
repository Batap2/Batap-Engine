#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{

struct CelestialBody_C
{
    // G*M rather than the mass: the mass alone would be a huge number that
    // never appears anywhere else.
    float mu_ = 1.f;
    float radius_ = 1.f;

    // A Static collider pins the body; a Kinematic one orbits under the pull of
    // the others. Nothing ever makes it Dynamic (see CelestialBody_S).
    v3f velocity_ = v3f::Zero();

    // Editor action: fills velocity_ for a circular orbit around whichever body
    // pulls hardest here, then clears itself.
    bool circularize_ = false;
};

static_assert(refl::fieldName<CelestialBody_C, 0>() == "mu");
static_assert(refl::fieldName<CelestialBody_C, 1>() == "radius");
static_assert(refl::fieldName<CelestialBody_C, 2>() == "velocity");
static_assert(refl::fieldName<CelestialBody_C, 3>() == "circularize");

BATAP_COMPONENT(CelestialBody_C, "celestialBody",
                fieldMeta<&CelestialBody_C::mu_>({.speed = 100.f, .min = 0.f, .max = 1e9f}),
                fieldMeta<&CelestialBody_C::radius_>({.speed = 1.f, .min = 0.01f, .max = 1e6f}));

}  // namespace batap
