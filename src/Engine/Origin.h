#pragma once

#include "EigenTypes.h"

#include <entt/entt.hpp>

namespace batap
{

struct Origin
{
    v3d pos_ = v3d::Zero();
    entt::entity anchor_ = entt::null;
};

}  // namespace batap
