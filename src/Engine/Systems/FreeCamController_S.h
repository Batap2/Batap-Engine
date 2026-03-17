#pragma once

namespace batap
{

struct Context;
struct World;

struct FreeCamController_S
{
    void update(Context& ctx, World& world, float deltaTime);
};
}  // namespace batap
