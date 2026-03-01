#include "TestScene.h"

#include "Components/Camera_C.h"
#include "Components/Transform_C.h"
#include "InputManager.h"
#include "Systems/Systems.h"
#include "Systems/TransformSystem.h"
#include "World.h"
#include "Instance/EntityFactory.h"
#include "Scene.h"

#include <numbers>

namespace batap
{
TestScene::TestScene(World& world) : Scene(*world.instanceManager_)
{
    _camera = world.entityFactory_->createCamera(_registry);
    auto camC = _camera.try_get<Camera_C>();
    camC->_active = true;
    camC->_znear = 0.1f;
    camC->_zfar = 1000;
    camC->_fov = std::numbers::pi_v<float> / 3;
}

void TestScene::update(float deltaTime, Context& ctx, World& world)
{
    float ts = 100.0f * deltaTime;
    if (ctx._inputManager->IsKeyDown(Key::D))
    {
        world.systems_->_transforms->translate(_camera, v3f(ts, 0, 0));
    }
    if (ctx._inputManager->IsKeyDown(Key::Q))
    {
        world.systems_->_transforms->translate(_camera, v3f(-ts, 0, 0));
    }
    if (ctx._inputManager->IsKeyDown(Key::E))
    {
        world.systems_->_transforms->translate(_camera, v3f(0, ts, 0));
    }
    if (ctx._inputManager->IsKeyDown(Key::A))
    {
        world.systems_->_transforms->translate(_camera, v3f(0, -ts, 0));
    }
    if (ctx._inputManager->IsKeyDown(Key::Z))
    {
        world.systems_->_transforms->translate(_camera, v3f(0, 0, ts));
    }
    if (ctx._inputManager->IsKeyDown(Key::S))
    {
        world.systems_->_transforms->translate(_camera, v3f(0, 0, -ts));
    }
}
}  // namespace batap
