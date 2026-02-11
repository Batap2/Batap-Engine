#pragma once

#include <entt/entt.hpp>
#include "Context.h"

#include <memory>

namespace batap
{

struct TransformSystem;

struct Systems
{
    Systems(Context& ctx);
    ~Systems();

    void update(float deltaTime, entt::registry& reg);

    std::unique_ptr<TransformSystem> _transforms;

    Context& _ctx;
};
}  // namespace batap
