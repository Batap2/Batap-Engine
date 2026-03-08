#pragma once

namespace batap
{

struct Context;
struct World;

struct FreeCamControllerSystem
{
    void update(Context& ctx, World& world, float deltaTime);
};
}  // namespace batap
