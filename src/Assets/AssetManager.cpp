#pragma once

#include "AssetManager.h"
#include <utility>
#include "Handles.h"

namespace rayvox
{
std::pair<AssetHandle, Mesh*> AssetManager::emplaceMesh(std::optional<std::string> name) {
    AssetHandle h;
    if(name){
        h = AssetHandle(AssetHandle::ObjectType::Mesh, name.value());
    } else {
        h = AssetHandle(AssetHandle::ObjectType::Mesh);
    }
    if(!_meshes.contains(h)){
        _meshes[h] = {};
    }
    return std::make_pair(h, &_meshes[h]);
}
}  // namespace rayvox