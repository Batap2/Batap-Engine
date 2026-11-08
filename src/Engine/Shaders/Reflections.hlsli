#ifndef BATAP_REFLECTIONS_HLSLI
#define BATAP_REFLECTIONS_HLSLI

#include "ShaderInterop.h"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<uint4> g_uintTextures[];

static const float ReflectMirrorRoughness = 0.0505f;
static const uint ReflectKindNone = 0u;
static const uint ReflectKindMesh = 1u;
static const uint ReflectKindTerrain = 2u;
static const uint ReflectKindScatter = 3u;
static const float ReflectSkyDistance = 65000.0f;
static const float ReflectMaxRadiance = 256.0f;

struct ReflectSurface
{
    float3 N_;
    float roughness_;
    uint kind_;
    uint id_;
};

uint ReflectOctPack(float3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 p = n.xy;
    if (n.z < 0.0f)
        p = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
    const uint2 q = uint2(round(saturate(p * 0.5f + 0.5f) * 65535.0f));
    return q.x | (q.y << 16);
}

float3 ReflectOctUnpack(uint v)
{
    const float2 p = float2(v & 0xFFFFu, v >> 16) / 65535.0f * 2.0f - 1.0f;
    float3 n = float3(p, 1.0f - abs(p.x) - abs(p.y));
    const float t = saturate(-n.z);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return normalize(n);
}

uint2 ReflectSurfaceEncode(float3 N, float roughness, uint kind, uint id)
{
    const uint r = uint(round(saturate(roughness) * 1023.0f));
    return uint2(ReflectOctPack(N), (r << 22) | (kind << 20) | (id & 0xFFFFFu));
}

ReflectSurface ReflectSurfaceDecode(uint2 v)
{
    ReflectSurface s;
    s.N_ = ReflectOctUnpack(v.x);
    s.roughness_ = float(v.y >> 22) / 1023.0f;
    s.kind_ = (v.y >> 20) & 3u;
    s.id_ = v.y & 0xFFFFFu;
    return s;
}

float ReflectRoughness(uint shadingModel, float roughness)
{
    return shadingModel == ShadingLit ? roughness : 1.0f;
}

float ReflectWeight(float roughness)
{
    return roughness <= ReflectMirrorRoughness ? 1.0f : 0.0f;
}

#endif
