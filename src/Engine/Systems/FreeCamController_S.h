#pragma once

namespace batap
{

struct Engine;
struct World;

struct FreeCamController_S
{
    void update(Engine& ctx, World& world, float deltaTime);
};
}  // namespace batap
