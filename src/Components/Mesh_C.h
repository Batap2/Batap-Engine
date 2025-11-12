#pragma once

#include <vector>
#include <cstdint>

namespace rayvox
{
struct Mesh_C
{
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
};
}  // namespace rayvox