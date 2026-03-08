#pragma once

#include "EigenTypes.h"

namespace batap
{
struct PointLight_C
{
    v3f color_ = {1, 1, 1};
    float intensity_ = 1;
    float radius_ = 10;
    float falloff_ = 1;
    bool castShadows_ = false;
};
}  // namespace batap
