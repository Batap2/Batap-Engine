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

#define INIT(...) = __VA_ARGS__

#else
#define INIT(...)
#endif

enum DescriptorSetIndex : uint
{
    BindlessSet = 0,  // owned by ResourceManager
    FrameSet = 1,     // owned by ScenePasses
};

enum BindlessBinding : uint
{
    SamplerBinding = 0,
    // Compare-enabled sampler: SampleCmp returns the filtered result of the
    // depth test, which is what makes a PCF tap one instruction.
    ShadowSamplerBinding = 1,
    // Must stay the HIGHEST binding of the set: VARIABLE_DESCRIPTOR_COUNT
    TexturesBinding = 2,
};

enum FrameSetBinding : uint
{
    CamerasBinding = 0,
    InstancesBinding = 1,
    LightsBinding = 2,
    MaterialsBinding = 3,
    SkyboxBinding = 4,
    DebugShapeVertsBinding = 5,
    DebugShapesBinding = 6,
    BillboardsBinding = 7,
    SphereOccludersBinding = 8,
    ShadowsBinding = 9,
    FrameSetBindingCount = 10,
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
    float3 pos_;
    float znear_;
    float3 right_;
    float zfar_;
    float3 up_;
    float fov_;
    float3 fwd_;
    float pad_;
};

struct StaticMeshGPUData
{
    float4x4 world_;
    uint materialIndices_[8];  // GPU arena slot per submesh
};

// Every kind of light, in one buffer and one shading loop. Each field means
// one thing, and a kind leaves the ones it has no use for at zero.
enum LightType : uint
{
    LightPoint = 0,
    LightSpot = 1,
    LightDirectional = 2,  // step 11b
    LightRect = 3,         // step 15
};

struct LightGPUData
{
    float3 pos_;
    float intensity_;
    float3 color_;
    float radius_;
    float3 direction_;
    float falloff_;
    uint type_;
    // Low 24 bits: index of this light's FIRST ShadowGPUData entry, its views
    // being contiguous. High 8: which ShadowFamily, hence how many follow and
    // which atlas they address. InvalidGPUIndex when the light has no shadow.
    uint shadowIndex_;
    float cosInner_;
    float cosOuter_;
    float3 halfWidth_;
    float sourceRadius_;  // metres, or an angular radius in radians for a directional
    float3 halfHeight_;
    float pad_;
};

enum ShadowFamily : uint
{
    ShadowLocalSingle = 0,    // atlas B, one tile
    ShadowLocalCube = 1,      // atlas B, six tiles
    ShadowCascadeFamily = 2,  // atlas A, ShadowCascadeCount quadrants
};

// Range the cascades are configured for, the partition line between the two
// shadow methods. A renderer setting rather than a per-light one since 7c.
// Step 12 replaces the test with the cascades' real coverage, and this goes.
static const float CascadeRange = 2000.0f;

// A cube's six faces, the most any light draws. Every casting light reserves
// that many entries from its shadowIndex_ on, at a slot fixed by its pool
// index, so fill() can write the index once while the allocation changes
// every frame.
static const uint MaxShadowViewsPerLight = 6u;

static const uint ShadowIndexMask = 0xFFFFFFu;
static const uint ShadowFamilyShift = 24u;

struct ShadowGPUData
{
    float4x4 viewProj_;
    // 0 when the view has no map this frame, which reads as lit. Below 1 while
    // the light fades out with distance.
    float strength_;
    // Texel world size per unit of distance from the light, this view being a
    // perspective one. A cascade is orthographic and will store an absolute
    // size here instead.
    float texelWorld_;
    // Bindless slot of the atlas image this view lives in.
    uint atlasTexture_;
    // The view's tile as a rectangle in atlas UV: tiles come in several sizes.
    float uvScale_;
    float uvOffset_[2];
    // Side of one atlas texel in UV, for the PCF kernel: the tile is a
    // sub-rectangle at native resolution, so a texel of it is a texel of the
    // atlas whatever uvScale_ says.
    float texelUV_;
    float pad_;
};

struct SphereOccluderGPUData
{
    float3 center_;
    float radius_;
};

// Also the material asset: it is uploaded to its arena as-is.
struct Material
{
    float4 albedo INIT({1.f, 1.f, 1.f, 1.f});
    float roughness INIT(0.3f);
    float metallic INIT(0.f);
    float reflectivity INIT(0.f);
    uint albedoTexIdx_ INIT(InvalidGPUIndex);
    uint normalTexIdx_ INIT(InvalidGPUIndex);
    uint roughnessTexIdx_ INIT(InvalidGPUIndex);
    uint metallicTexIdx_ INIT(InvalidGPUIndex);
    uint shadingModel_ INIT(0u);  // ShadingModel
};

struct SkyboxGPUData
{
    float4 sh[9];        // SH L2 irradiance, already scaled by intensity
    uint mode;           // 0 = HDRI, 1 = FlatColor, 2 = Gradient
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
    float3 pos_;
    float pad_;
};

struct DebugShapeGPUData
{
    float4x4 world_;
    float4 color_;
};

struct BillboardGPUData
{
    float3 pos_;
    float sizeX_;
    float4 tint_;
    float4 rot_;  // only read when BillboardFixed
    float sizeY_;
    uint materialIdx_;
    uint textureIdx_;  // overrides the material albedo map when valid
    uint flags_;       // bit 0: size is a fraction of screen height, bit 1: Y-locked, bit 2: fixed
};

static const uint BillboardScreenSize = 1u;
static const uint BillboardCylindrical = 2u;
static const uint BillboardFixed = 4u;

struct DrawPush
{
    uint cameraIndex_;
    uint instanceIndex_;
    uint submeshIndex_;
    uint lightCount_;
    uint skyboxCount_;
    uint sphereOccluderCount_;
    // Which ShadowGPUData the shadow pass is drawing into. One view per draw,
    // so the six faces of step 7 differ only by this.
    uint shadowViewIndex_;
};

#ifdef __cplusplus

static_assert(sizeof(CameraGPUData) == 192);
static_assert(sizeof(StaticMeshGPUData) == 96);
static_assert(sizeof(LightGPUData) == 96);
static_assert(sizeof(Material) == 48);
static_assert(sizeof(SkyboxGPUData) == 224);
static_assert(sizeof(DrawPush) == 28);
static_assert(sizeof(SphereOccluderGPUData) == 16);
static_assert(sizeof(ShadowGPUData) == 96);
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
              gpuStrideOk<LightGPUData> && gpuStrideOk<Material> &&
              gpuStrideOk<SkyboxGPUData> && gpuStrideOk<DebugVertexGPUData> &&
              gpuStrideOk<DebugShapeGPUData> && gpuStrideOk<BillboardGPUData> &&
              gpuStrideOk<ShadowGPUData>);

static_assert(offsetof(CameraGPUData, pos_) == 128 && offsetof(CameraGPUData, znear_) == 140);
static_assert(offsetof(SkyboxGPUData, color1) == 160);
static_assert(offsetof(Material, albedoTexIdx_) == 28);

}  // namespace batap

#endif
