#pragma once

#include <volk.h>

#include "Renderer/Device/Pipelines.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>

namespace batap
{
struct VulkanContext;

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
    TerrainMorphPS,
    TonemapPS,
    ExposureMeasurePS,
    PrepassPS,
    GeometryRtPS,
    TerrainRtPS,
    RtDebugPS,
    GiUpdatePS,
    GiResolvePS,
    GiVotePS,
    GiTracePS,
    ScatterVS,
    ScatterPS,
    ScatterShadowVS,
    ScatterRtPS,
    TranslucentPS,
    TranslucentRtPS,
    TranslucentCompositePS,
    BillboardTranslucentPS,
    BloomDownPS,
    BloomUpPS,
    SkyEquirectPS,
    SkyPrefilterPS,
    AoPS,
    AoFilterPS,
    AoUpsamplePS,
    PrepassMeshRtPS,
    PrepassTerrainRtPS,
    PrepassScatterRtPS,
    ReflectTracePS,
    ReflectResolvePS,
    ReflectTemporalPS,
    ReflectFilterPS,
    ScreenShadowPS,
    ImpostorVS,
    ImpostorPS,
    ImpostorRtPS,
    ImpostorPrepassPS,
    ImpostorPrepassRtPS,
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
    {"TerrainMorphPS", "TerrainMorphPS", "ps_6_6"},
    {"TonemapPS", "TonemapPS", "ps_6_6"},
    {"ExposureMeasurePS", "ExposureMeasurePS", "ps_6_6"},
    {"PrepassPS", "PrepassPS", "ps_6_6"},
    {"PixelShaderRT", "PixelShader", "ps_6_6", true},
    {"TerrainPSRT", "TerrainPS", "ps_6_6", true},
    {"RtDebugPS", "RtDebugPS", "ps_6_6", true},
    {"GiUpdatePS", "GiUpdatePS", "ps_6_6", true},
    {"GiResolvePS", "GiResolvePS", "ps_6_6", true},
    {"GiVotePS", "GiVotePS", "ps_6_6", true},
    {"GiTracePS", "GiTracePS", "ps_6_6", true},
    {"ScatterVS", "ScatterVS", "vs_6_6"},
    {"ScatterPS", "ScatterPS", "ps_6_6"},
    {"ScatterShadowVS", "ScatterShadowVS", "vs_6_6"},
    {"ScatterPSRT", "ScatterPS", "ps_6_6", true},
    {"TranslucentPS", "TranslucentPS", "ps_6_6"},
    {"TranslucentPSRT", "TranslucentPS", "ps_6_6", true},
    {"TranslucentCompositePS", "TranslucentCompositePS", "ps_6_6"},
    {"BillboardTranslucentPS", "BillboardTranslucentPS", "ps_6_6"},
    {"BloomDownPS", "BloomDownPS", "ps_6_6"},
    {"BloomUpPS", "BloomUpPS", "ps_6_6"},
    {"SkyEquirectPS", "SkyEquirectPS", "ps_6_6"},
    {"SkyPrefilterPS", "SkyPrefilterPS", "ps_6_6"},
    {"AoPS", "AoPS", "ps_6_6"},
    {"AoFilterPS", "AoFilterPS", "ps_6_6"},
    {"AoUpsamplePS", "AoUpsamplePS", "ps_6_6"},
    {"PrepassMeshRtPS", "PrepassMeshRtPS", "ps_6_6"},
    {"PrepassTerrainRtPS", "PrepassTerrainRtPS", "ps_6_6"},
    {"PrepassScatterRtPS", "PrepassScatterRtPS", "ps_6_6"},
    {"ReflectTracePS", "ReflectTracePS", "ps_6_6", true},
    {"ReflectResolvePS", "ReflectResolvePS", "ps_6_6"},
    {"ReflectTemporalPS", "ReflectTemporalPS", "ps_6_6"},
    {"ReflectFilterPS", "ReflectFilterPS", "ps_6_6"},
    {"ScreenShadowPS", "ScreenShadowPS", "ps_6_6"},
    {"ImpostorVS", "ImpostorVS", "vs_6_6"},
    {"ImpostorPS", "ImpostorPS", "ps_6_6"},
    {"ImpostorPSRT", "ImpostorPS", "ps_6_6", true},
    {"ImpostorPrepassPS", "ImpostorPrepassPS", "ps_6_6"},
    {"ImpostorPrepassRtPS", "ImpostorPrepassRtPS", "ps_6_6"},
}};

using ShaderModules = std::array<VkShaderModule, ShaderCount>;

struct ShaderLibrary
{
    explicit ShaderLibrary(VulkanContext& ctx);

    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    const ShaderModules& modules() const { return modules_; }

    bool reloadIfChanged();

   private:
    VulkanContext& ctx_;
    std::array<std::optional<ShaderModule>, ShaderCount> owned_;
    ShaderModules modules_{};
    std::filesystem::file_time_type sourcesMtime_{};
};
}  // namespace batap
