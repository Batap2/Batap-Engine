#pragma once
#include "Assets/AssetHandle.h"
#include <array>
#include <cstdint>

namespace batap
{

struct Materials_C
{
    std::array<MaterialHandle, 8> slots{};
    uint8_t count = 0;
};

}  // namespace batap
