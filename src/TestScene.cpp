#include "TestScene.h"
#include "Components/Camera_C.h"
#include "Scene.h"

namespace rayvox{
    TestScene::TestScene(GPUInstanceManager& instanceManager) : Scene(instanceManager){
        _camera = _registry.create();
        _registry.emplace<Camera_C>(_camera);
    }
}
