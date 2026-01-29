#pragma once

#include "Components/EntityHandle.h"
#include "Scene.h"

namespace rayvox
{
struct TestScene : Scene
{
    TestScene(Context& ctx);

    void update(float deltaTime) override;

    EntityHandle _camera;
};
}  // namespace rayvox
