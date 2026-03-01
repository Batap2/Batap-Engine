#pragma once

#include <entt/entt.hpp>

#include <memory>
#include "Context.h"

namespace batap
{

struct Context;
struct TransformSystem;
struct World;

struct Systems
{
    Systems();
    ~Systems();

    void update(float deltaTime, Context& ctx, World& world);

    std::unique_ptr<TransformSystem> _transforms;
};
}  // namespace batap
