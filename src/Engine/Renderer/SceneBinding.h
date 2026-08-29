#pragma once

#include "entt/entt.hpp"

namespace batap
{
struct Engine;
struct World;
struct GPUInstanceManager;

struct SceneRenderArgs
{
    entt::registry* reg_ = nullptr;
    GPUInstanceManager* instanceManager_ = nullptr;
};

void bindScene(Engine& ctx, World& world);
}  // namespace batap
