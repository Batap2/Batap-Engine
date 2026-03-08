#include "TestScene.h"

#include "Components/Camera_C.h"
#include "Components/Transform_C.h"
#include "Components/FreeCamController_C.h"
#include "InputManager.h"
#include "Instance/EntityFactory.h"
#include "Scene.h"
#include "Systems/Systems.h"
#include "Systems/TransformSystem.h"
#include "World.h"

#include <numbers>

namespace batap
{
TestScene::TestScene(World& world) : Scene(*world.instanceManager_)
{
    _camera = world.entityFactory_->createCamera(_registry);
    auto& camController = _camera.emplace<FreeCamController_C>();
    camController.controlled_ = true;
    camController.requireRightMouseButton_ = true;

    world.systems_->_transforms->translate(_camera, v3f(0,2,6), Space::Local);

    auto camC = _camera.try_get<Camera_C>();
    camC->_active = true;
    camC->_znear = 0.1f;
    camC->_zfar = 1000;
    camC->_fov = std::numbers::pi_v<float> / 3;
}

void TestScene::update(float deltaTime, Context& ctx, World& world)
{
}
}  // namespace batap
