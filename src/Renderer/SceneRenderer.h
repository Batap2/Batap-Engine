#pragma once

#include "entt/entt.hpp"

#include "Renderer.h"
#include "Scene.h"

#include <cstdint>
#include <vector>

namespace rayvox
{
struct SceneRenderer
{
    SceneRenderer(Renderer* renderer) : _renderer(renderer) {}

    void initRenderPasses();
    void loadScene(Scene* scene);
    void uploadDirty();

   private:
    Scene* _scene = nullptr;
    Renderer* _renderer;
};
}  // namespace rayvox