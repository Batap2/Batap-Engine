#include "VoxelRayScene.h"
#include "Components/Camera_C.h"

namespace rayvox{
    VoxelRayScene::VoxelRayScene(){
        _camera = _registry.create();
        _registry.emplace<Camera_C>(_camera);
    }
}