#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct Planetshine_C
{
    float radius_ = 1;
    col3 albedo_ = {0.3f, 0.3f, 0.3f};
};

BATAP_COMPONENT(Planetshine_C, "planetshine", ComponentMeta{.color = ComponentColor::Orange},
                fieldMeta<&Planetshine_C::radius_>({.speed = 0.01f, .min = 0.f, .max = 1e9f}));
}  // namespace batap
