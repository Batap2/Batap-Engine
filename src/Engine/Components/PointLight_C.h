#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct PointLight_C
{
    col3 color_ = {1, 1, 1};
    float intensity_ = 100;
    float radius_ = 10;
    float falloff_ = 0;
    bool castShadows_ = false;

    float sourceRadius_ = 0;
};

BATAP_COMPONENT(PointLight_C, "pointLight", ComponentMeta{.color = ComponentColor::Orange},
                fieldMeta<&PointLight_C::intensity_>({.speed = 1.f, .min = 0.f, .max = 1e9f}));
}  // namespace batap
