#pragma once

#include "Handles.h"


#include <memory>
#include <unordered_map>
#include <optional>

namespace rayvox
{

struct ResourceManager;
struct Mesh;

struct AssetManager
{
    AssetManager(ResourceManager& rm);
    ~AssetManager();

    std::unordered_map<AssetHandle, std::unique_ptr<Mesh>> _meshes;

    std::pair<AssetHandle, Mesh*> emplaceMesh(std::optional<std::string> name = std::nullopt);

    ResourceManager& _resourceManager;
};
}  // namespace rayvox
