// Compiled twice: as HLSL by dxc, as C++ by the engine. Everything the two must
// agree on lives here — GPU structs and binding numbers.

#pragma once

#ifdef __cplusplus

#include <cstddef>
#include <cstdint>

namespace batap
{
using uint = uint32_t;
using float3 = float[3];
using float4 = float[4];
using float4x4 = float[16];

#define BATAP_INIT(...) = __VA_ARGS__

#else
#define BATAP_INIT(...)
#endif

enum DescriptorSetIndex : uint
{
    BindlessSet = 0,  // owned by ResourceManager
    FrameSet = 1,     // owned by ScenePasses
};

enum BindlessBinding : uint
{
    SamplerBinding = 0,
    TexturesBinding = 1,
};

enum FrameSetBinding : uint
{
    CamerasBinding = 0,
    InstancesBinding = 1,
    PointLightsBinding = 2,
    MaterialsBinding = 3,
    SkyboxBinding = 4,
    FrameSetBindingCount = 5,
};

static const uint InvalidGPUIndex = 0xFFFFFFFFu;

struct CameraGPUData
{
    float4x4 view_;
    float4x4 proj_;
    float3 pos_;   float znear_;
    float3 right_; float zfar_;
    float3 up_;    float fov_;
    float3 fwd_;   float pad_;
};

struct StaticMeshGPUData
{
    float4x4 world_;
    uint materialIndices_[8];  // GPU arena slot per submesh
};

struct PointLightGPUData
{
    float3 pos_;   float intensity_;
    float3 color_; float radius_;
    float falloff_;
    uint castShadows_;
};

// Also the material asset: it is uploaded to its arena as-is.
struct Material
{
    float4 albedo BATAP_INIT({1.f, 1.f, 1.f, 1.f});
    float roughness BATAP_INIT(0.3f);
    float metallic BATAP_INIT(0.f);
    float reflectivity BATAP_INIT(0.f);
    uint albedoTexIdx_ BATAP_INIT(InvalidGPUIndex);
    uint normalTexIdx_ BATAP_INIT(InvalidGPUIndex);
    uint roughnessTexIdx_ BATAP_INIT(InvalidGPUIndex);
    uint metallicTexIdx_ BATAP_INIT(InvalidGPUIndex);
    uint pad_ BATAP_INIT(0u);
};

struct SkyboxGPUData
{
    float4 sh[9];  // SH L2 irradiance, already scaled by intensity
    uint mode;     // 0 = HDRI, 1 = FlatColor, 2 = Gradient
    uint bindlessIndex;  // HDRI texture
    uint mipCount;
    float intensity;
    float4 color1;  // zenith — flat mode: the single color
    float4 color2;  // horizon
    float4 color3;  // nadir
    float horizonWidth;
    float3 pad;
};

struct DrawPush
{
    uint cameraIndex_;
    uint instanceIndex_;
    uint submeshIndex_;
    uint pointLightCount_;
};

#ifdef __cplusplus

static_assert(sizeof(CameraGPUData) == 192);
static_assert(sizeof(StaticMeshGPUData) == 96);
static_assert(sizeof(PointLightGPUData) == 40);
static_assert(sizeof(Material) == 48);
static_assert(sizeof(SkyboxGPUData) == 224);
static_assert(sizeof(DrawPush) == 16);

static_assert(offsetof(CameraGPUData, pos_) == 128 && offsetof(CameraGPUData, znear_) == 140);
static_assert(offsetof(SkyboxGPUData, color1) == 160);
static_assert(offsetof(Material, albedoTexIdx_) == 28);

}  // namespace batap

#endif
