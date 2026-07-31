#pragma once

#include "entt/entt.hpp"

#include "Engine.h"
#include "Scene.h"

#include <cstdint>
#include <vector>

namespace batap
{

struct SceneRenderArgs
{
    entt::registry* reg_ = nullptr;
    GPUInstanceManager* instanceManager_ = nullptr;
};

struct SceneRenderer
{
    SceneRenderer(Engine& ctx) : _ctx(ctx) {}

    void setScene(SceneRenderArgs args) { args_ = args; }

    void initRenderPasses();
    void uploadDirty();

   private:
    Engine& _ctx;
    SceneRenderArgs args_;
};
}  // namespace batap
