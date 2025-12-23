#pragma once

#include "Assets/AssetManager.h"
#include "Handles.h"
#include <string_view>

namespace rayvox{
    std::vector<AssetHandle> importFile(std::string_view path, AssetManager& assetM);
}
