#include "AssetManager.h"

#include "Handles.h"
#include "Mesh.h"

#include <cstddef>
#include <memory>
#include <utility>

namespace batap
{
AssetManager::AssetManager(ResourceManager* rm) : _resourceManager(rm) {}
AssetManager::~AssetManager() = default;

AssetManager::MeshEmplaceResult AssetManager::emplaceMesh(std::optional<std::string> name)
{
    AssetHandle h;
    bool alreadyExist = false;
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
    else
    {
        alreadyExist = true;
    }
    return {_meshes.at(h).get(), h, alreadyExist};
}
}  // namespace batap

// AssetManager -> ResourceManager
