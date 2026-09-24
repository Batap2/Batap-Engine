#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct SpotLight_C
{
    col3 color_ = {1, 1, 1};
    float intensity_ = 1;
    float radius_ = 10;
    float falloff_ = 1;
    bool castShadows_ = false;
    float innerAngle_ = 0.35f;
    float outerAngle_ = 0.52f;
};

BATAP_COMPONENT(SpotLight_C, "spotLight", ComponentMeta{.color = ComponentColor::Orange});
}  // namespace batap
