#ifndef BATAP_GICACHE_HLSLI
#define BATAP_GICACHE_HLSLI

#include "ShaderInterop.h"

#ifndef BATAP_RT
#define BATAP_RT 0
#endif

static const float GiCellBase = 0.15f;
static const float GiLevelDistance = 7.5f;
static const uint GiLevelCount = 16u;
static const uint GiFrameMesh = 0u;
static const uint GiFrameChunk = 1u;

struct GiPoint
{
    uint frame_;
    float3 local_;
    float3 nLocal_;
};

float3 GiFacing(float3 n, float3 posRel)
{
    return dot(n, posRel) > 0.0f ? -n : n;
}

GiPoint GiPointOf(uint kind, uint id, float3x3 world, float3 tRel, float3 posRel, float3 Ngeom)
{
    const float3 ax = normalize(world._m00_m10_m20);
    const float3 ay = normalize(world._m01_m11_m21);
    const float3 az = normalize(world._m02_m12_m22);
    const float3 d = posRel - tRel;
    GiPoint p;
    p.frame_ = (kind << 19) | (id & 0x7FFFFu);
    p.local_ = float3(dot(ax, d), dot(ay, d), dot(az, d));
    p.nLocal_ = float3(dot(ax, Ngeom), dot(ay, Ngeom), dot(az, Ngeom));
    return p;
}

uint GiLevel(float distance)
{
    const int level = int(floor(log2(max(distance, 1e-3f) / GiLevelDistance)));
    return uint(clamp(level, 0, int(GiLevelCount) - 1));
}

float GiCellSize(uint level)
{
    return GiCellBase * float(1u << level);
}

// Dominant half-axis, not the octant: a face flat on an object axis has a
// normal component at exactly zero, and its sign is float noise, different
// between screen derivatives and the ray's triangle.
// A facet at exactly 45 degrees has two equal components, and the screen's
// derivative normal and the ray's triangle normal order them by noise: the
// tie goes to X, then Y, on both sides.
static const float GiSideTie = 1e-3f;

uint GiSide(GiPoint p)
{
    const float3 a = abs(p.nLocal_);
    const uint axis = a.x + GiSideTie >= a.y ? (a.x + GiSideTie >= a.z ? 0u : 2u)
                                             : (a.y + GiSideTie >= a.z ? 1u : 2u);
    return axis * 2u + (p.nLocal_[axis] < 0.0f ? 1u : 0u);
}

uint2 GiCacheKey(GiPoint p, uint level)
{
    const float size = GiCellSize(level);
    const uint3 cell = uint3(clamp(int3(floor(p.local_ / size)) + 2048, 0, 4095));
    const uint side = GiSide(p);
    const uint hi = 0x80000000u | (p.frame_ << 11) | (level << 7) | (side << 4) | (cell.x >> 8);
    const uint lo = (cell.x << 24) | (cell.y << 12) | cell.z;
    return uint2(hi, lo);
}

uint GiHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float3 GiSideColor(uint side)
{
    const float3 axis = side / 2u == 0u ? float3(1.0f, 0.0f, 0.0f)
                        : side / 2u == 1u ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
    return axis * ((side & 1u) != 0u ? 0.35f : 1.0f);
}

float3 GiDebugColor(uint2 key)
{
    const uint h = GiHash(key.y ^ GiHash(key.x));
    return float3(float(h & 255u), float((h >> 8) & 255u), float((h >> 16) & 255u)) / 255.0f * 0.85f +
           0.15f;
}

float GiRand(inout uint seed)
{
    seed = GiHash(seed);
    return float(seed >> 8) / 16777216.0f;
}

float GiLuma(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

static const uint GiChangeMinSamples = 16u;
static const float GiChangeRatio = 0.5f;
static const float GiChangeFloor = 0.02f;

#if BATAP_RT
[[vk::binding(RtCacheKeysBinding, RayTracingSet)]] RWStructuredBuffer<uint64_t> GiKeys;
[[vk::binding(RtCacheValuesBinding, RayTracingSet)]] RWStructuredBuffer<RtCacheValueGPUData> GiValues;
[[vk::binding(RtCacheStatsBinding, RayTracingSet)]] RWStructuredBuffer<uint> GiStats;

static const uint GiNoSlot = 0xFFFFFFFFu;

void GiStatAdd(uint stat, uint bucket)
{
    InterlockedAdd(GiStats[stat * RtCacheStatsBuckets + bucket], 1u);
}

uint64_t GiKey64(uint2 key)
{
    return (uint64_t(key.x) << 32) | uint64_t(key.y);
}

uint GiFirstSlot(uint2 key)
{
    return GiHash(key.y ^ GiHash(key.x)) & (RtCacheCapacity - 1u);
}

uint GiSlotFind(uint2 key)
{
    const uint64_t key64 = GiKey64(key);
    uint slot = GiFirstSlot(key);
    [loop]
    for (uint i = 0; i < RtCacheProbes; ++i)
    {
        const uint64_t stored = GiKeys[slot];
        if (stored == key64)
            return slot;
        if (stored == 0)
            return GiNoSlot;
        slot = (slot + 1u) & (RtCacheCapacity - 1u);
    }
    return GiNoSlot;
}

uint GiSlotInsert(uint2 key, out bool inserted)
{
    const uint64_t key64 = GiKey64(key);
    uint slot = GiFirstSlot(key);
    inserted = false;
    [loop]
    for (uint i = 0; i < RtCacheProbes; ++i)
    {
        uint64_t stored;
        InterlockedCompareExchange(GiKeys[slot], 0, key64, stored);
        inserted = stored == 0;
        if (inserted || stored == key64)
            return slot;
        slot = (slot + 1u) & (RtCacheCapacity - 1u);
    }
    return GiNoSlot;
}

void GiAccumulate(uint slot, float3 irradiance)
{
    const uint3 fixed = uint3(round(max(irradiance, 0.0f) * RtCacheScale));
    InterlockedAdd(GiValues[slot].sum_[0], fixed.x);
    InterlockedAdd(GiValues[slot].sum_[1], fixed.y);
    InterlockedAdd(GiValues[slot].sum_[2], fixed.z);
    InterlockedAdd(GiValues[slot].count_, 1u);
}

float3 GiIrradiance(uint slot)
{
    const RtCacheValueGPUData v = GiValues[slot];
    return float3(v.irradiance_[0], v.irradiance_[1], v.irradiance_[2]);
}
#endif

#endif
