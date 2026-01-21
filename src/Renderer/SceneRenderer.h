#pragma once

#include "entt/entt.hpp"

#include "Context.h"
#include "Scene.h"

#include <cstdint>
#include <vector>

namespace rayvox
{
struct SceneRenderer
{
    SceneRenderer(Context& ctx) : _ctx(ctx) {}

    void initRenderPasses();
    void loadScene(Scene* scene);
    void uploadDirty();

   private:
    Scene* _scene = nullptr;
    Context& _ctx;
};
}  // namespace rayvox
