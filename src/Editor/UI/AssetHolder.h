#pragma once

#include <imgui.h>
#include "EigenTypes.h"

namespace batap
{
struct AssetHolderConfig
{
    v2f pos_;
    v2f size_;
    ImTextureID _thumbnail;
};

bool AssetHolder(AssetHolderConfig config);
}  // namespace batap
