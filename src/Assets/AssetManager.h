#pragma once

#include "Handles.h"

#include "emhash/hash_table8.hpp"

#include <memory>
#include <optional>

namespace batap
{

struct ResourceManager;
struct Mesh;

struct AssetManager
{
    AssetManager(ResourceManager* rm);
    ~AssetManager();

    emhash8::HashMap<AssetHandle, std::unique_ptr<Mesh>> _meshes;

    struct MeshEmplaceResult
    {
        Mesh* _mesh = nullptr;
        AssetHandle _handle;
        bool _alreadyExist = false;
    };
    MeshEmplaceResult emplaceMesh(std::optional<std::string> name = std::nullopt);

    ResourceManager* _resourceManager;
};
}  // namespace batap
