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
    DebugShapeVertsBinding = 5,
    DebugShapesBinding = 6,
    BillboardsBinding = 7,
    FrameSetBindingCount = 8,
};

enum ShadingModel : uint
{
    ShadingLit = 0,
    ShadingUnlit = 1,
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
    float pad_[2];
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
    uint shadingModel_ BATAP_INIT(0u);  // ShadingModel
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

// One vertex of a unit wireframe, built once at startup. A debug shape is that
// wireframe under a matrix, so nothing is tessellated per frame.
struct DebugVertexGPUData
{
    float3 pos_; float pad_;
};

struct DebugShapeGPUData
{
    float4x4 world_;
    float4 color_;
};

struct BillboardGPUData
{
    float3 pos_;  float sizeX_;
    float4 tint_;
    float4 rot_;  // only read when BillboardFixed
    float sizeY_;
    uint materialIdx_;
    uint textureIdx_;  // overrides the material albedo map when valid
    uint flags_;  // bit 0: size is a fraction of screen height, bit 1: Y-locked, bit 2: fixed
};

static const uint BillboardScreenSize = 1u;
static const uint BillboardCylindrical = 2u;
static const uint BillboardFixed = 4u;

struct DrawPush
{
    uint cameraIndex_;
    uint instanceIndex_;
    uint submeshIndex_;
    uint pointLightCount_;
    uint skyboxCount_;
};

#ifdef __cplusplus

static_assert(sizeof(CameraGPUData) == 192);
static_assert(sizeof(StaticMeshGPUData) == 96);
static_assert(sizeof(PointLightGPUData) == 48);
static_assert(sizeof(Material) == 48);
static_assert(sizeof(SkyboxGPUData) == 224);
static_assert(sizeof(DrawPush) == 20);
static_assert(sizeof(DebugVertexGPUData) == 16);
static_assert(sizeof(DebugShapeGPUData) == 80);
static_assert(sizeof(BillboardGPUData) == 64);

// dxc lays a structured buffer out as std430: the array stride is the struct
// size rounded up to 16. A C++ struct that is not a multiple of 16 therefore
// writes at a smaller stride than the shader reads, and every element past the
// first lands on garbage.
template <class T>
inline constexpr bool gpuStrideOk = sizeof(T) % 16 == 0;
static_assert(gpuStrideOk<CameraGPUData> && gpuStrideOk<StaticMeshGPUData> &&
              gpuStrideOk<PointLightGPUData> && gpuStrideOk<Material> &&
              gpuStrideOk<SkyboxGPUData> && gpuStrideOk<DebugVertexGPUData> &&
              gpuStrideOk<DebugShapeGPUData> && gpuStrideOk<BillboardGPUData>);

static_assert(offsetof(CameraGPUData, pos_) == 128 && offsetof(CameraGPUData, znear_) == 140);
static_assert(offsetof(SkyboxGPUData, color1) == 160);
static_assert(offsetof(Material, albedoTexIdx_) == 28);

}  // namespace batap

#endif
