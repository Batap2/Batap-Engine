#pragma once

#include "Assets/AssetHandle.h"

#include <string_view>
#include <vector>

namespace batap
{
struct AssetManager;

std::vector<MeshHandle> importMeshFromFile(std::string_view path, AssetManager& assetManager);
}  // namespace batap
