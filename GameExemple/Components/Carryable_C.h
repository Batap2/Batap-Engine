#pragma once

#include "Reflection/ComponentRegistry.h"

namespace batap
{

struct Carryable_C
{
    // Scales what the grabber can lift, so a crate can be made awkward without
    // touching its mass.
    float grip_ = 1.f;
};

static_assert(refl::fieldName<Carryable_C, 0>() == "grip");

BATAP_COMPONENT(Carryable_C, "carryable",
                fieldMeta<&Carryable_C::grip_>({.speed = 0.01f, .min = 0.f, .max = 10.f}));

}  // namespace batap
