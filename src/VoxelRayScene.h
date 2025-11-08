#pragma once

#include "Scene.h"

namespace rayvox
{
struct VoxelRayScene : Scene
{
    VoxelRayScene();
    
    void update(float deltaTime) override{};
    
    entt::entity _camera;
};
}  // namespace rayvox