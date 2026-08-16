#pragma once

#include <entt/entt.hpp>

#include <memory>
#include "Engine.h"

namespace batap
{

struct Engine;
struct Transform_S;
struct World;
struct FreeCamController_S;

struct Systems
{
    Systems();
    ~Systems();

    void update(float deltaTime, Engine& ctx, World& world);

    std::unique_ptr<FreeCamController_S> freecam_;
    std::unique_ptr<Transform_S> transforms_;
};
}  // namespace batap
