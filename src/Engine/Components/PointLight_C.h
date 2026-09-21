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

    float sourceRadius_ = 0;

    // Range of the cascades. The two shadow methods partition space: cascades
    // inside it, analytic occluders beyond. An occluder wholly contained within
    // this distance of the camera stays silent, or both would shadow the same
    // caster.
    float shadowDistance_ = 2000;
};

// Fields, json keys and UI are derived from the struct.
BATAP_COMPONENT(PointLight_C, "pointLight", ComponentMeta{.color = ComponentColor::Orange});
}  // namespace batap
