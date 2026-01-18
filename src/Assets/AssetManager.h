#pragma once

#include "Handles.h"

#include <memory>
#include <optional>
#include <unordered_map>


namespace rayvox
{

struct ResourceManager;
struct Mesh;

struct AssetManager
{
    AssetManager(ResourceManager* rm);
    ~AssetManager();

    std::unordered_map<AssetHandle, std::unique_ptr<Mesh>> _meshes;

    struct MeshEmplaceResult
    {
        Mesh* _mesh = nullptr;
        AssetHandle _handle;
        bool _alreadyExist = false;
    };
    MeshEmplaceResult emplaceMesh(std::optional<std::string> name = std::nullopt);

    ResourceManager* _resourceManager;
};
}  // namespace rayvox
