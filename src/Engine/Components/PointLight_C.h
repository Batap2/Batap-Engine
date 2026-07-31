#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct PointLight_C
{
    col3 color_ = {1, 1, 1};
    float intensity_ = 1;
    float radius_ = 10;
    float falloff_ = 1;
    bool castShadows_ = false;
};

// Fields, json keys and UI are derived from the struct.
BATAP_COMPONENT(PointLight_C, "pointLight", ComponentMeta{.flag = ComponentFlag::PointLight});
}  // namespace batap
