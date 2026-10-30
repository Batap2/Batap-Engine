// Compiled twice: as HLSL by dxc, as C++ by the engine. Everything the two must
// agree on lives here — GPU structs and binding numbers.

#pragma once

#ifdef __cplusplus

#include <cstddef>
#include <cstdint>

namespace batap
{
using uint = uint32_t;
using int3 = int32_t[3];
using float3 = float[3];
using float4 = float[4];
using float4x4 = float[16];

#define INIT(...) = __VA_ARGS__

#else
#define INIT(...)
#endif

enum DescriptorSetIndex : uint
{
    BindlessSet = 0,    // owned by ResourceManager
    FrameSet = 1,       // owned by FrameBindings
    RayTracingSet = 2,  // owned by FrameBindings
};

enum RayTracingSetBinding : uint
{
    TlasBinding = 0,
    RtGeometriesBinding = 1,
    RtInstancesBinding = 2,
    RtCacheKeysBinding = 3,
    RtCacheValuesBinding = 4,
    RtCacheStatsBinding = 5,
    RtCacheWorkBinding = 6,
    RtCacheProbesBinding = 7,
};

static const uint TlasMaskShadowCaster = 1u;
static const uint TlasMaskNoShadow = 2u;

struct RtGeometryGPUData
{
    uint indicesAddrLo_;
    uint indicesAddrHi_;
    uint verticesAddrLo_;
    uint verticesAddrHi_;
    uint vertexStride_;
    uint indexBytes_;
    uint pad0_;
    uint pad1_;
};

static const uint RtInstanceMesh = 0u;
static const uint RtInstanceChunk = 1u;

struct RtInstanceGPUData
{
    uint firstGeometry_;
    uint gpuIndex_;
    uint kind_;
    uint pad0_;
};

// Keys are uint64, 0 = empty slot; values are fixed-point sums.
static const uint RtCacheCapacity = 1u << 20;
static const uint RtCacheProbes = 16u;
static const float RtCacheScale = 1024.0f;
static const float RtCacheSampleMax = 4194304.0f;
static const uint RtCacheResolveWidth = 1024u;
static const uint RtCacheStatsBuckets = 256u;
static const uint RtCacheStatOccupied = 0u;
static const uint RtCacheStatInserted = 1u;
static const uint RtCacheStatFailed = 2u;
static const uint RtCacheStatEvicted = 3u;
static const uint RtCacheStatTailMiss = 4u;
static const uint RtCacheStatCount = 5u;
static const float GiCellBase = 0.15f;

static const uint RtCacheListCapacity = 1u << 16;
static const uint RtCacheTraceWidth = 256u;
static const uint RtCacheCandidateBits = 19u;
static const uint RtCacheCandidateCapacity = 1u << RtCacheCandidateBits;
static const uint RtCacheCandidateWords = 6u;
static const uint RtCacheWorkCount = 0u;
static const uint RtCacheWorkList = 1u;
static const uint RtCacheWorkCandidates = RtCacheWorkList + RtCacheListCapacity;
static const uint RtCacheWorkWords =
    RtCacheWorkCandidates + RtCacheCandidateCapacity * RtCacheCandidateWords;

static const uint RtProbeBest = 0u;
static const uint RtProbeDirection = 1u;
static const uint RtProbeOrigin = 2u;
static const uint RtProbeCamera = 5u;
static const uint RtProbeRadiance = 8u;
static const uint RtProbeChange = 9u;
static const uint RtProbeState = 10u;
static const uint RtProbeBorn = 11u;
static const uint RtProbeEvent = 12u;
static const uint RtProbeWords = 13u;
static const uint RtProbeNone = 0u;
static const uint RtProbeFresh = 1u;
static const uint RtProbeArmed = 2u;

struct RtCacheValueGPUData
{
    float irradiance_[3];
    uint history_;
    uint sum_[3];
    uint count_;
    float filtered_[3];
    uint vote_;
};

enum BindlessBinding : uint
{
    SamplerBinding = 0,
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
    FrameConstantsBinding = 10,
    TerrainChunksBinding = 11,
    TerrainVerticesBinding = 12,
    TerrainWorldsBinding = 13,
    TerrainTracedBinding = 14,
    TerrainMorphRowsBinding = 15,
    FrameSetBindingCount = 16,
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
    float pad0_;
    float3 up_;
    float fov_;
    float3 fwd_;
    float pad_;
    // The frame before, as view_ and proj_ apply it: the view's rotation only.
    float4x4 prevViewProj_;
    float3 prevPos_;
    float pad1_;
};

struct StaticMeshGPUData
{
    float4x4 world_;
    uint materialIndices_[8];  // GPU arena slot per submesh
    float4x4 prevWorld_;
};

// The normals are octahedral, two snorm16 in a uint (Terrain.hlsli). The
// targets are the vertex on its parent's shape, then its grandparent's.
struct TerrainVertexGPUData
{
    float3 position_;
    uint normal_;
    float3 target_;
    uint targetNormal_;
    float3 target2_;
    uint target2Normal_;
};

// Written once per chunk, never in place: a change takes a new index, since
// frames in flight still read the old one.
struct TerrainChunkGPUData
{
    uint materialIndices_[8];  // per stratum
    uint vertexBase_;
    uint cells_;
    // Sides 0..3 (j = 0, i = cells_, j = cells_, i = 0) and corners 4..7
    // ((0,0), (cells_,0), (cells_,cells_), (0,cells_)) pinned to the first target.
    uint force_;
    uint flags_;
    // The first target's band; the second's is twice as far.
    float morphStart_;
    float morphEnd_;
    uint level_;
    uint pad0_;
};

static const uint TerrainMorphs = 1u;
// Terrain_C::debug_: the chunk's border drawn in its level's colour.
static const uint TerrainOutline = 2u;

// Every frame, at the chunk's index: chunk space to world space less the
// render camera's position, worked out in double (Terrain_S::publish).
struct TerrainWorldGPUData
{
    float4x4 world_;
    int3 centreCell_;
    uint owner_;
    float3 centreOffset_;
    float pad0_;
    float4x4 prevWorld_;
};

struct TerrainMorphRowGPUData
{
    uint instance_;
    uint firstVertex_;
    uint vertices_;
    uint traced_;
};

static const uint TerrainMorphRowWidth = 256u;

// A kind leaves the fields it has no use for at zero.
enum LightType : uint
{
    LightPoint = 0,
    LightSpot = 1,
    LightDirectional = 2,  // takes the cascades
    LightRect = 3,         // pos_ the centre, direction_ the normal
    LightFarPoint = 4,     // shaded as a point light; takes the cascades
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
    // Low 24 bits: the first of the light's contiguous ShadowGPUData entries.
    // High 8: its ShadowFamily.
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

// Up to this full door angle, one view from the apex of the doors' pyramid:
// every ray from it to a lit point crosses the face. Wider, the pyramid
// flattens toward a half-space no view holds, and a cube takes over.
static const float RectShadowSingleViewMaxAngle = 2.6179939f;  // 150 degrees

// Every casting light reserves this many entries at a slot fixed by its pool
// index, so fill() writes shadowIndex_ once while the allocation changes every
// frame.
static const uint MaxShadowViewsPerLight = 6u;

// One quadrant of atlas A each.
static const uint ShadowCascadeCount = 4u;

static const uint ShadowIndexMask = 0xFFFFFFu;
static const uint ShadowFamilyShift = 24u;

struct ShadowGPUData
{
    float4x4 viewProj_;
    // 0 when the view has no map this frame, which reads as lit.
    float strength_;
    // Per unit of distance from the light, except a directional light's
    // orthographic cascades: in metres.
    float texelWorld_;
    uint atlasTexture_;
    float uvScale_;
    float uvOffset_[2];
    // Tiles are at native resolution: an atlas texel whatever uvScale_ says.
    float texelUV_;
    float pad_;
    // Cascades only.
    float4 sphere_;
};

static const uint ExposureGridSize = 64u;
static const uint ExposureReduceBlock = 8u;

enum TonemapOperator : uint
{
    TonemapNone = 0,
    TonemapAgx = 1,
    TonemapPbrNeutral = 2,
    TonemapAces = 3,
    TonemapReinhard = 4,
    TonemapUchimura = 5,
};

struct FrameGPUData
{
    // Renderer/Scene/LtcTables.h
    uint ltcMatTexture_;
    uint ltcAmpTexture_;
    uint frameIndex_;
    uint giWidth_;
    uint giHeight_;
    uint giMaxAge_;
    float giDt_;
    uint giBounces_;
    uint giPasses_;
    uint giPerCell_;
    uint hdrTexture_;
    uint tonemap_;
    float exposure_;
    uint pad0_;
    uint pad1_;
    uint pad2_;
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
    uint shadowViewIndex_;
    uint debugFlags_;
};

static const uint DebugShadowCascades = 1u;
static const uint DebugRtFirstHit = 2u;
static const uint DebugGiCells = 4u;
static const uint DebugGiSides = 8u;
static const uint DebugGiCache = 16u;
static const uint DebugMotionVectors = 32u;

#ifdef __cplusplus

static_assert(sizeof(CameraGPUData) == 272);
static_assert(sizeof(StaticMeshGPUData) == 160);
static_assert(sizeof(LightGPUData) == 96);
static_assert(sizeof(Material) == 48);
static_assert(sizeof(SkyboxGPUData) == 224);
static_assert(sizeof(DrawPush) == 32);
static_assert(sizeof(SphereOccluderGPUData) == 16);
static_assert(sizeof(FrameGPUData) == 64);
static_assert(sizeof(ShadowGPUData) == 112);
static_assert(ShadowCascadeCount <= MaxShadowViewsPerLight);
static_assert(sizeof(DebugVertexGPUData) == 16);
static_assert(sizeof(DebugShapeGPUData) == 80);
static_assert(sizeof(BillboardGPUData) == 64);
static_assert(sizeof(TerrainVertexGPUData) == 48);
static_assert(sizeof(TerrainChunkGPUData) == 64);
static_assert(sizeof(TerrainWorldGPUData) == 160);
static_assert(sizeof(TerrainMorphRowGPUData) == 16);
static_assert(sizeof(RtGeometryGPUData) == 32);
static_assert(sizeof(RtInstanceGPUData) == 16);
static_assert(sizeof(RtCacheValueGPUData) == 48);
static_assert(RtCacheCapacity % RtCacheResolveWidth == 0);
static_assert(RtCacheTraceWidth * RtCacheTraceWidth == RtCacheListCapacity);

// dxc lays a structured buffer out as std430: the array stride is the struct
// size rounded up to 16. A C++ struct that is not a multiple of 16 therefore
// writes at a smaller stride than the shader reads, and every element past the
// first lands on garbage.
template <class T>
inline constexpr bool gpuStrideOk = sizeof(T) % 16 == 0;
static_assert(gpuStrideOk<CameraGPUData> && gpuStrideOk<StaticMeshGPUData> &&
              gpuStrideOk<LightGPUData> && gpuStrideOk<Material> && gpuStrideOk<SkyboxGPUData> &&
              gpuStrideOk<DebugVertexGPUData> && gpuStrideOk<DebugShapeGPUData> &&
              gpuStrideOk<BillboardGPUData> && gpuStrideOk<ShadowGPUData> &&
              gpuStrideOk<TerrainVertexGPUData> && gpuStrideOk<TerrainChunkGPUData> &&
              gpuStrideOk<TerrainWorldGPUData> && gpuStrideOk<RtGeometryGPUData> &&
              gpuStrideOk<RtInstanceGPUData> && gpuStrideOk<RtCacheValueGPUData>);

static_assert(offsetof(CameraGPUData, pos_) == 128 && offsetof(CameraGPUData, znear_) == 140);
static_assert(offsetof(SkyboxGPUData, color1) == 160);
static_assert(offsetof(Material, albedoTexIdx_) == 28);

}  // namespace batap

#endif
