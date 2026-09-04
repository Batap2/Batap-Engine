#include "TestScene.h"

#include "Components/Camera_C.h"
#include "Components/FreeCamController_C.h"
#include "Components/Transform_C.h"
#include "InputManager.h"
#include "Instance/EntityFactory.h"
#include "Scene.h"
#include "Systems/Systems.h"
#include "Systems/Transform_S.h"
#include "World.h"


#include <numbers>

namespace batap
{
TestScene::TestScene(World& world) : Scene(*world.instanceManager_)
{
    camera_ = world.entityFactory_->create(registry_, spawnableFor(EntityKind::Camera));
    auto& camController = camera_.emplace<FreeCamController_C>();
    camController.controlled_ = true;
    camController.requireRightMouseButton_ = true;

    world.systems_->transforms_->translate(camera_, v3f(0, 2, 6), Space::Local);

    auto camC = camera_.try_get<Camera_C>();
    camC->active_ = true;
    camC->znear_ = 0.1f;
    camC->zfar_ = 1000;
    camC->fov_ = std::numbers::pi_v<float> / 3;
}

void TestScene::update(float deltaTime, Engine& ctx, World& world) {}
}  // namespace batap
