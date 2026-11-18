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
    ExposureMeasureCS,
    PrepassPS,
    GeometryRtPS,
    TerrainRtPS,
    RtDebugPS,
    GiUpdateCS,
    GiResolveCS,
    GiVoteCS,
    GiTraceCS,
    ScatterVS,
    ScatterPS,
    ScatterShadowVS,
    ScatterRtPS,
    TranslucentPS,
    TranslucentRtPS,
    TranslucentCompositePS,
    BillboardTranslucentPS,
    BloomDownCS,
    BloomUpCS,
    SkyEquirectCS,
    SkyPrefilterCS,
    AoCS,
    AoFilterCS,
    AoUpsampleCS,
    PrepassMeshRtPS,
    PrepassTerrainRtPS,
    PrepassScatterRtPS,
    ReflectTraceCS,
    ReflectTraceOwnCS,
    ReflectResolveCS,
    ReflectTemporalCS,
    ReflectFilterCS,
    ScreenShadowCS,
    ImpostorVS,
    ImpostorPS,
    ImpostorRtPS,
    ImpostorPrepassPS,
    ImpostorPrepassRtPS,
    ScatterCullCS,
    ScatterLateCS,
    ScatterHzbCS,
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
    {"PixelShader", "PixelShader", "ps_6_7"},
    {"SkyVS", "SkyVS", "vs_6_6"},
    {"SkyPS", "SkyPS", "ps_6_6"},
    {"DebugShapeVS", "DebugShapeVS", "vs_6_6"},
    {"DebugPS", "DebugPS", "ps_6_6"},
    {"BillboardVS", "BillboardVS", "vs_6_6"},
    {"BillboardPS", "BillboardPS", "ps_6_6"},
    {"ShadowVS", "ShadowVS", "vs_6_6"},
    {"TerrainVS", "TerrainVS", "vs_6_6"},
    {"TerrainPS", "TerrainPS", "ps_6_7"},
    {"TerrainShadowVS", "TerrainShadowVS", "vs_6_6"},
    {"TerrainMorphPS", "TerrainMorphPS", "ps_6_6"},
    {"TonemapPS", "TonemapPS", "ps_6_6"},
    {"ExposureMeasureCS", "ExposureMeasureCS", "cs_6_6"},
    {"PrepassPS", "PrepassPS", "ps_6_6"},
    {"PixelShaderRT", "PixelShader", "ps_6_7", true},
    {"TerrainPSRT", "TerrainPS", "ps_6_7", true},
    {"RtDebugPS", "RtDebugPS", "ps_6_6", true},
    {"GiUpdateCS", "GiUpdateCS", "cs_6_6", true},
    {"GiResolveCS", "GiResolveCS", "cs_6_6", true},
    {"GiVoteCS", "GiVoteCS", "cs_6_6", true},
    {"GiTraceCS", "GiTraceCS", "cs_6_6", true},
    {"ScatterVS", "ScatterVS", "vs_6_6"},
    {"ScatterPS", "ScatterPS", "ps_6_7"},
    {"ScatterShadowVS", "ScatterShadowVS", "vs_6_6"},
    {"ScatterPSRT", "ScatterPS", "ps_6_7", true},
    {"TranslucentPS", "TranslucentPS", "ps_6_6"},
    {"TranslucentPSRT", "TranslucentPS", "ps_6_6", true},
    {"TranslucentCompositePS", "TranslucentCompositePS", "ps_6_6"},
    {"BillboardTranslucentPS", "BillboardTranslucentPS", "ps_6_6"},
    {"BloomDownCS", "BloomDownCS", "cs_6_6"},
    {"BloomUpCS", "BloomUpCS", "cs_6_6"},
    {"SkyEquirectCS", "SkyEquirectCS", "cs_6_6"},
    {"SkyPrefilterCS", "SkyPrefilterCS", "cs_6_6"},
    {"AoCS", "AoCS", "cs_6_6"},
    {"AoFilterCS", "AoFilterCS", "cs_6_6"},
    {"AoUpsampleCS", "AoUpsampleCS", "cs_6_6"},
    {"PrepassMeshRtPS", "PrepassMeshRtPS", "ps_6_6"},
    {"PrepassTerrainRtPS", "PrepassTerrainRtPS", "ps_6_6"},
    {"PrepassScatterRtPS", "PrepassScatterRtPS", "ps_6_6"},
    {"ReflectTraceCS", "ReflectTraceCS", "cs_6_6", true},
    {"ReflectTraceOwnCS", "ReflectTraceOwnCS", "cs_6_6", true},
    {"ReflectResolveCS", "ReflectResolveCS", "cs_6_6"},
    {"ReflectTemporalCS", "ReflectTemporalCS", "cs_6_6"},
    {"ReflectFilterCS", "ReflectFilterCS", "cs_6_6"},
    {"ScreenShadowCS", "ScreenShadowCS", "cs_6_6"},
    {"ImpostorVS", "ImpostorVS", "vs_6_6"},
    {"ImpostorPS", "ImpostorPS", "ps_6_7"},
    {"ImpostorPSRT", "ImpostorPS", "ps_6_7", true},
    {"ImpostorPrepassPS", "ImpostorPrepassPS", "ps_6_6"},
    {"ImpostorPrepassRtPS", "ImpostorPrepassRtPS", "ps_6_6"},
    {"ScatterCullCS", "ScatterCullCS", "cs_6_6"},
    {"ScatterLateCS", "ScatterLateCS", "cs_6_6"},
    {"ScatterHzbCS", "ScatterHzbCS", "cs_6_6"},
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
