#ifndef BATAP_GICACHE_HLSLI
#define BATAP_GICACHE_HLSLI

#include "ShaderInterop.h"

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

uint GiOctant(GiPoint p)
{
    return (p.nLocal_.x > 0.0f ? 1u : 0u) | (p.nLocal_.y > 0.0f ? 2u : 0u) |
           (p.nLocal_.z > 0.0f ? 4u : 0u);
}

uint2 GiCacheKey(GiPoint p, uint level)
{
    const float size = GiCellSize(level);
    const uint3 cell = uint3(clamp(int3(floor(p.local_ / size)) + 2048, 0, 4095));
    const uint octant = GiOctant(p);
    const uint hi = 0x80000000u | (p.frame_ << 11) | (level << 7) | (octant << 4) | (cell.x >> 8);
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

float3 GiOctantColor(uint octant)
{
    return float3((octant & 1u) != 0u ? 1.0f : 0.15f, (octant & 2u) != 0u ? 1.0f : 0.15f,
                  (octant & 4u) != 0u ? 1.0f : 0.15f);
}

float3 GiDebugColor(uint2 key)
{
    const uint h = GiHash(key.y ^ GiHash(key.x));
    return float3(float(h & 255u), float((h >> 8) & 255u), float((h >> 16) & 255u)) / 255.0f * 0.85f +
           0.15f;
}

#endif
