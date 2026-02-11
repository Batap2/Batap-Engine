#pragma once

#include "Assets/AssetManager.h"
#include "Handles.h"

#include <string_view>

namespace batap
{
struct AssetManager;
std::vector<AssetHandle> importMeshFromFile(std::string_view path, AssetManager& assetManager);
}  // namespace batap
