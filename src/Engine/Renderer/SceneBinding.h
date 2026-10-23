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
    entt::entity camera_ = entt::null;
    bool showShadowCascades_ = false;
    bool showRtFirstHit_ = false;
    bool showGiCells_ = false;
    bool showGiSides_ = false;
    bool showGiCache_ = false;
};

void bindScene(Engine& ctx, World& world);
}  // namespace batap
