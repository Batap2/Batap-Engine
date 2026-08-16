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
    SceneRenderer(Engine& ctx) : ctx_(ctx) {}

    void setScene(SceneRenderArgs args) { args_ = args; }

    void initRenderPasses();
    void uploadDirty();

   private:
    Engine& ctx_;
    SceneRenderArgs args_;
};
}  // namespace batap
