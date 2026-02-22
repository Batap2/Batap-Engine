#pragma once

#include <imgui.h>
#include "EigenTypes.h"

namespace batap
{
struct AssetHolderConfig
{
    v2f _pos;
    v2f _size;
    ImTextureID _thumbnail;
};

void AssetHolder();
}  // namespace batap
