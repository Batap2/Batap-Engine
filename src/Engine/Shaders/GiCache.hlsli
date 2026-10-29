#ifndef BATAP_GICACHE_HLSLI
#define BATAP_GICACHE_HLSLI

#include "ShaderInterop.h"

#ifndef BATAP_RT
#define BATAP_RT 0
#endif

static const float GiLevelDistance = 3.75f;
static const uint GiLevelCount = 16u;
static const uint GiFrameMesh = 0u;
static const uint GiFrameTerrain = 1u;

struct GiPoint
{
    uint frame_;
    int3 cell_;
    float3 local_;
    float3 nLocal_;
};

float3 GiFacing(float3 n, float3 posRel)
{
    return dot(n, posRel) > 0.0f ? -n : n;
}

GiPoint GiPointOf(uint kind, uint id, int3 cell, float3 offset, float3x3 world, float3 tRel,
                  float3 posRel, float3 Ngeom)
{
    const float3 ax = normalize(world._m00_m10_m20);
    const float3 ay = normalize(world._m01_m11_m21);
    const float3 az = normalize(world._m02_m12_m22);
    const float3 d = posRel - tRel;
    GiPoint p;
    p.frame_ = (kind << 19) | (id & 0x7FFFFu);
    p.cell_ = cell;
    p.local_ = offset + float3(dot(ax, d), dot(ay, d), dot(az, d));
    p.nLocal_ = float3(dot(ax, Ngeom), dot(ay, Ngeom), dot(az, Ngeom));
    return p;
}

GiPoint GiMeshPoint(uint instance, float3x3 world, float3 tRel, float3 posRel, float3 Ngeom)
{
    return GiPointOf(GiFrameMesh, instance, int3(0, 0, 0), float3(0.0f, 0.0f, 0.0f), world, tRel,
                     posRel, Ngeom);
}

GiPoint GiTerrainPoint(TerrainWorldGPUData chunk, float3 posRel, float3 Ngeom)
{
    return GiPointOf(GiFrameTerrain, chunk.owner_, chunk.centreCell_, chunk.centreOffset_,
                     (float3x3)chunk.world_, chunk.world_._m03_m13_m23, posRel, Ngeom);
}

void GiCellAt(GiPoint p, uint level, out int3 cell, out float3 frac)
{
    const float3 g = p.local_ / GiCellBase;
    const float3 whole = floor(g);
    const int3 fine = p.cell_ + int3(whole);
    const int shift = int(level);
    const int span = int(1u << level);
    cell = fine >> shift;
    frac = (float3(fine & (span - 1)) + (g - whole)) / float(span);
}

static const float GiLevelBlend = 0.25f;

float GiLevelOf(float distance)
{
    return log2(max(distance, 1e-3f) / GiLevelDistance);
}

uint GiLevelAt(float level)
{
    return uint(clamp(int(floor(level)), 0, int(GiLevelCount) - 1));
}

uint GiLevel(float distance)
{
    return GiLevelAt(GiLevelOf(distance));
}

uint GiRandomLevel(float distance, float u)
{
    return GiLevelAt(GiLevelOf(distance) + (2.0f * u - 1.0f) * GiLevelBlend);
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
    if ((p.frame_ >> 19) == GiFrameTerrain)
        return 0u;
    const float3 a = abs(p.nLocal_);
    const uint axis = a.x + GiSideTie >= a.y ? (a.x + GiSideTie >= a.z ? 0u : 2u)
                                             : (a.y + GiSideTie >= a.z ? 1u : 2u);
    return axis * 2u + (p.nLocal_[axis] < 0.0f ? 1u : 0u);
}

uint2 GiCellKey(uint frame, int3 cell, uint side, uint level)
{
    const uint3 c = uint3(cell) & 0xFFFu;
    const uint hi = 0x80000000u | (frame << 11) | (level << 7) | (side << 4) | (c.x >> 8);
    const uint lo = (c.x << 24) | (c.y << 12) | c.z;
    return uint2(hi, lo);
}

uint2 GiCacheKey(GiPoint p, uint level)
{
    int3 cell;
    float3 frac;
    GiCellAt(p, level, cell, frac);
    return GiCellKey(p.frame_, cell, GiSide(p), level);
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

static const float GiSlowTau = 1.0f;
static const bool GiStratified = false;
static const float GiShowSamples = 8.0f;
static const uint GiSamplesMask = 0xFFFFu;

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

uint GiReserve(uint slot)
{
    uint before;
    InterlockedAdd(GiValues[slot].count_, 1u, before);
    return before;
}

void GiAccumulate(uint slot, float3 irradiance, float exposure)
{
    const uint3 fixed =
        uint3(round(min(max(irradiance, 0.0f) * (RtCacheScale * exposure), RtCacheSampleMax)));
    InterlockedAdd(GiValues[slot].sum_[0], fixed.x);
    InterlockedAdd(GiValues[slot].sum_[1], fixed.y);
    InterlockedAdd(GiValues[slot].sum_[2], fixed.z);
}

float2 GiR2(uint index, uint2 rotation)
{
    const uint2 x = index * uint2(3242174889u, 2447445414u) + rotation;
    return float2(x) / 4294967296.0f;
}

float3 GiIrradiance(uint slot)
{
    const RtCacheValueGPUData v = GiValues[slot];
    return float3(v.irradiance_[0], v.irradiance_[1], v.irradiance_[2]);
}

float GiConfidence(RtCacheValueGPUData v)
{
    return saturate(float(v.history_ & GiSamplesMask) / GiShowSamples);
}

float3 GiNeighbourAverage(uint2 key, float3 self, float selfConfidence)
{
    const uint frame = (key.x >> 11) & 0xFFFFFu;
    const uint level = (key.x >> 7) & 0xFu;
    const uint side = (key.x >> 4) & 0x7u;
    const int3 cell =
        int3(((key.x & 0xFu) << 8) | (key.y >> 24), (key.y >> 12) & 0xFFFu, key.y & 0xFFFu);
    float3 sum = self * (8.0f * selfConfidence);
    float weight = 8.0f * selfConfidence;
    [loop]
    for (uint n = 0; n < 27u; ++n)
    {
        const int3 o = int3(n % 3u, (n / 3u) % 3u, n / 9u) - 1;
        const int taxicab = abs(o.x) + abs(o.y) + abs(o.z);
        if (taxicab == 0 || taxicab == 3)
            continue;
        const uint slot = GiSlotFind(GiCellKey(frame, cell + o, side, level));
        if (slot == GiNoSlot)
            continue;
        const RtCacheValueGPUData v = GiValues[slot];
        const float w = float(8u >> uint(taxicab)) * GiConfidence(v);
        sum += w * float3(v.irradiance_[0], v.irradiance_[1], v.irradiance_[2]);
        weight += w;
    }
    return weight > 0.0f ? sum / weight : self;
}

struct GiGather
{
    float3 sum_;
    float found_;
    float trusted_;
};

void GiGatherLevel(GiPoint p, uint side, uint level, float levelWeight, inout GiGather g)
{
    int3 cell;
    float3 frac;
    GiCellAt(p, level, cell, frac);
    const float3 shifted = frac - 0.5f;
    const float3 below = floor(shifted);
    const int3 base = cell + int3(below);
    const float3 f = shifted - below;
    [unroll]
    for (uint corner = 0; corner < 8u; ++corner)
    {
        const uint3 o = uint3(corner, corner >> 1, corner >> 2) & 1u;
        const uint slot = GiSlotFind(GiCellKey(p.frame_, base + int3(o), side, level));
        if (slot == GiNoSlot)
            continue;
        const RtCacheValueGPUData v = GiValues[slot];
        const float3 w = lerp(1.0f - f, f, float3(o));
        const float weight = levelWeight * w.x * w.y * w.z;
        const float trust = weight * GiConfidence(v);
        g.sum_ += trust * float3(v.filtered_[0], v.filtered_[1], v.filtered_[2]);
        g.found_ += weight;
        g.trusted_ += trust;
    }
}

float3 GiIrradianceAt(GiPoint p, float distance, float3 fallback)
{
    const float level = GiLevelOf(distance);
    const uint lo = GiLevelAt(level - GiLevelBlend);
    const uint hi = GiLevelAt(level + GiLevelBlend);
    const float hiWeight =
        hi == lo ? 0.0f : (level + GiLevelBlend - float(hi)) / (2.0f * GiLevelBlend);
    const uint side = GiSide(p);
    GiGather g = (GiGather)0;
    GiGatherLevel(p, side, lo, 1.0f - hiWeight, g);
    if (hiWeight > 0.0f)
        GiGatherLevel(p, side, hi, hiWeight, g);
    return g.found_ > 0.0f ? (g.sum_ + (g.found_ - g.trusted_) * fallback) / g.found_ : fallback;
}
#endif

#endif
