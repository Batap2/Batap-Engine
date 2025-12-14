#pragma once

#include "Assethandle.h"
#include "Mesh.h"

#include <unordered_map>

namespace rayvox
{
struct AssetManager
{
    std::unordered_map<Assethandle, Mesh> _meshes;
};
}  // namespace rayvox