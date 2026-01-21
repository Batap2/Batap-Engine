#pragma once

#include "Scene.h"

namespace rayvox
{
struct TestScene : Scene
{
    TestScene(GPUInstanceManager& instanceManager);

    void update(float deltaTime) override {}

    entt::entity _camera;
};
}  // namespace rayvox
