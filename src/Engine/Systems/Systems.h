#pragma once

#include <entt/entt.hpp>

#include <memory>
#include "Context.h"

namespace batap
{

struct Context;
struct Transform_S;
struct World;
struct FreeCamController_S;

struct Systems
{
    Systems();
    ~Systems();

    void update(float deltaTime, Context& ctx, World& world);

    std::unique_ptr<FreeCamController_S> freecam_;
    std::unique_ptr<Transform_S> _transforms;
};
}  // namespace batap
