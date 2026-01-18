#include "TestScene.h"
#include "Components/Camera_C.h"

namespace rayvox{
    TestScene::TestScene(){
        _camera = _registry.create();
        _registry.emplace<Camera_C>(_camera);
    }
}
