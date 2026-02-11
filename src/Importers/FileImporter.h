#pragma once

#include <string_view>
#include "Assets/AssetManager.h"
#include "Handles.h"


namespace batap
{
std::vector<AssetHandle> importFile(std::string_view path, AssetManager& assetM);
}
