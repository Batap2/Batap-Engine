#pragma once

#include "Components/EntityHandle.h"
#include "EigenTypes.h"

namespace batap
{

struct ContactEvent
{
    EntityHandle a_;
    EntityHandle b_;
    bool entered_ = false;

    v3f point_ = v3f::Zero();
    v3f normal_ = v3f::Zero();
    float closingSpeed_ = 0.f;
};

}  // namespace batap
