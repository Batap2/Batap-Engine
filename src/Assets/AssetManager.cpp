#include "AssetManager.h"

#include "Handles.h"
#include "Mesh.h"

#include <memory>
#include <utility>

namespace rayvox
{
AssetManager::AssetManager(ResourceManager* rm) : _resourceManager(rm) {}
AssetManager::~AssetManager() = default;

std::pair<AssetHandle, Mesh*> AssetManager::emplaceMesh(std::optional<std::string> name)
{
    AssetHandle h;
    if (name)
    {
        h = AssetHandle(AssetHandle::ObjectType::Mesh, name.value());
    }
    else
    {
        h = AssetHandle(AssetHandle::ObjectType::Mesh);
    }
    if (!_meshes.contains(h))
    {
        _meshes[h] = std::make_unique<Mesh>();
    }
    return std::make_pair(h, _meshes[h].get());
}
}  // namespace rayvox


// AssetManager -> ResourceManager
