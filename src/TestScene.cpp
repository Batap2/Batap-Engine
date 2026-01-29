#include "TestScene.h"
#include "Components/Transform_C.h"
#include "InputManager.h"
#include "Components/Camera_C.h"
#include "Instance/EntityFactory.h"
#include "Scene.h"

namespace rayvox
{
TestScene::TestScene(Context& ctx) : Scene(ctx)
{
    _camera = _ctx._entityFactory->createCamera(_registry);
}

void TestScene::update(float deltaTime){
    if(_ctx._inputManager->IsKeyDown(Key::Z)){
        auto transform = write<Transform_C>(_camera);

        transform->translate(v3f(0.1f,0,0));
    }
}
}  // namespace rayvox
