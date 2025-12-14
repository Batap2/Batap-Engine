#pragma once

#include "Handles.h"
#include "Mesh.h"

#include <unordered_map>

namespace rayvox
{
struct AssetManager
{
    std::unordered_map<AssetHandle, Mesh> _meshes;
};
}  // namespace rayvox