#include "TestScene.h"
#include "Components/Camera_C.h"
#include "Components/Transform_C.h"
#include "InputManager.h"

#include "Systems/Systems.h"
#include "Systems/TransformSystem.h"

#include "Instance/EntityFactory.h"
#include "Scene.h"

#include <numbers>

namespace batap
{
TestScene::TestScene(Context& ctx) : Scene(ctx)
{
    _camera = _ctx._entityFactory->createCamera(_registry);
    auto camC = _camera.try_get<Camera_C>();
    camC->_active = true;
    camC->_znear = 0.1f;
    camC->_zfar = 1000;
    camC->_fov = std::numbers::pi_v<float> / 3;
}

void TestScene::update(float deltaTime)
{
    float ts = 100.0f * deltaTime;
    if (_ctx._inputManager->IsKeyDown(Key::D))
    {
        _ctx._systems->_transforms->translate(_camera, v3f(ts, 0, 0));
    }
    if (_ctx._inputManager->IsKeyDown(Key::Q))
    {
        _ctx._systems->_transforms->translate(_camera, v3f(-ts, 0, 0));
    }
    if (_ctx._inputManager->IsKeyDown(Key::E))
    {
        _ctx._systems->_transforms->translate(_camera, v3f(0, ts, 0));
    }
    if (_ctx._inputManager->IsKeyDown(Key::A))
    {
        _ctx._systems->_transforms->translate(_camera, v3f(0, -ts, 0));
    }
    if (_ctx._inputManager->IsKeyDown(Key::Z))
    {
        _ctx._systems->_transforms->translate(_camera, v3f(0, 0, ts));
    }
    if (_ctx._inputManager->IsKeyDown(Key::S))
    {
        _ctx._systems->_transforms->translate(_camera, v3f(0, 0, -ts));
    }
}
}  // namespace batap
