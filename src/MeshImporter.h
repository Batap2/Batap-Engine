#pragma once

#include "Assets/Assethandle.h"

#include <string_view>

namespace rayvox
{
inline Assethandle importMeshFromFile(std::string_view path);
}