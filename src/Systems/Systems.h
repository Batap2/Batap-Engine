#pragma once

#include "Context.h"
#include <entt/entt.hpp>

#include <memory>

namespace rayvox
{

struct TransformSystem;

struct Systems
{
    Systems(Context& ctx);
    ~Systems();

    void update(float deltaTime, entt::registry &reg);

    std::unique_ptr<TransformSystem> _transforms;

    Context& _ctx;
};
}  // namespace rayvox
