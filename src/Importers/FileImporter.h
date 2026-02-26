#pragma once

#include <string_view>
#include "Assets/AssetManager.h"
#include "Handles.h"


namespace batap
{
std::vector<AssetHandleAny> importFile(std::string_view path, AssetManager& assetM);
}
