#pragma once

#include <imgui.h>
#include <string>
#include "EigenTypes.h"

namespace batap
{
struct AssetHolderConfig
{
    v2f         pos_;
    v2f         size_;
    ImTextureID thumbnail_;
    std::string label_;
};

bool AssetHolder(AssetHolderConfig config);
}  // namespace batap
