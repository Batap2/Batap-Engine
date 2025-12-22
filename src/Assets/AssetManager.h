#pragma once

#include "Handles.h"
#include "Mesh.h"

#include <unordered_map>
#include <optional>

namespace rayvox
{
struct AssetManager
{
    std::unordered_map<AssetHandle, Mesh> _meshes;

    std::pair<AssetHandle, Mesh*> emplaceMesh(std::optional<std::string> name = std::nullopt);
};
}  // namespace rayvox
