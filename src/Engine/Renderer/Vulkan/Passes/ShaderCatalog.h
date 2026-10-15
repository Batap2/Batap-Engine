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
    ShadowVS,
    TerrainVS,
    TerrainPS,
    TerrainShadowVS,
    GeometryRtPS,
    TerrainRtPS,
    RtDebugPS,
    ShaderCount,
};

struct ShaderDesc
{
    const char* name_;
    const char* source_;
    const char* target_;
    bool rayTracing_ = false;
};

inline constexpr const char* kRayTracingDefine = "BATAP_RT=1";

// The CMake shader rule builds name_.spv from source_.hlsl, with
// kRayTracingDefine when rayTracing_: the two must stay in step.
inline constexpr std::array<ShaderDesc, ShaderCount> kShaders = {{
    {"VertexShader", "VertexShader", "vs_6_6"},
    {"PixelShader", "PixelShader", "ps_6_6"},
    {"SkyVS", "SkyVS", "vs_6_6"},
    {"SkyPS", "SkyPS", "ps_6_6"},
    {"DebugShapeVS", "DebugShapeVS", "vs_6_6"},
    {"DebugPS", "DebugPS", "ps_6_6"},
    {"BillboardVS", "BillboardVS", "vs_6_6"},
    {"BillboardPS", "BillboardPS", "ps_6_6"},
    {"ShadowVS", "ShadowVS", "vs_6_6"},
    {"TerrainVS", "TerrainVS", "vs_6_6"},
    {"TerrainPS", "TerrainPS", "ps_6_6"},
    {"TerrainShadowVS", "TerrainShadowVS", "vs_6_6"},
    {"PixelShaderRT", "PixelShader", "ps_6_6", true},
    {"TerrainPSRT", "TerrainPS", "ps_6_6", true},
    {"RtDebugPS", "RtDebugPS", "ps_6_6", true},
}};

using ShaderModules = std::array<VkShaderModule, ShaderCount>;
}  // namespace batap
