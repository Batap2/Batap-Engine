#pragma once

#include "Components/EntityHandle.h"
#include "Scene.h"

namespace batap
{

struct World;

struct TestScene : Scene
{
    TestScene(World& world);

    void update(float deltaTime, Engine& ctx, World& world) override;

    EntityHandle _camera;
};
}  // namespace batap
