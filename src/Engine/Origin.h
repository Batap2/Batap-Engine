#pragma once

#include "EigenTypes.h"

#include <entt/entt.hpp>

namespace batap
{

struct Origin
{
    entt::entity anchor_ = entt::null;
    v3d offset_ = v3d::Zero();
    v3d pos_ = v3d::Zero();
};

}  // namespace batap
