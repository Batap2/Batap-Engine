#pragma once

#include "entt/entt.hpp"

#include "Context.h"
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
    SceneRenderer(Context& ctx) : _ctx(ctx) {}

    void setScene(SceneRenderArgs args) { args_ = args; }

    void initRenderPasses();
    void uploadDirty();

   private:
    Context& _ctx;
    SceneRenderArgs args_;
};
}  // namespace batap
