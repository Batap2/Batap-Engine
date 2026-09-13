#pragma once

#include <volk.h>

#include <array>
#include <cstddef>

namespace batap
{
enum ShaderId : size_t
{
    GeometryVS,
    GeometryPS,
    SkyVS,
    SkyPS,
    DebugVS,
    DebugPS,
    BillboardVS,
    BillboardPS,
    ShaderCount,
};

struct ShaderDesc
{
    const char* name_;
    const char* target_;
};

// name_ is the basename of both the .hlsl in the tree and the .spv the build
// drops next to the exe: the CMake shader rule keeps the two in step.
inline constexpr std::array<ShaderDesc, ShaderCount> kShaders = {{
    {"VertexShader", "vs_6_6"},
    {"PixelShader", "ps_6_6"},
    {"SkyVS", "vs_6_6"},
    {"SkyPS", "ps_6_6"},
    {"DebugShapeVS", "vs_6_6"},
    {"DebugPS", "ps_6_6"},
    {"BillboardVS", "vs_6_6"},
    {"BillboardPS", "ps_6_6"},
}};

using ShaderModules = std::array<VkShaderModule, ShaderCount>;
}  // namespace batap
