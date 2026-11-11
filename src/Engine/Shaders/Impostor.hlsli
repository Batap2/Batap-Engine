#ifndef BATAP_IMPOSTOR_HLSLI
#define BATAP_IMPOSTOR_HLSLI

#include "Scatter.hlsli"

static const uint ImpostorGrid = 8u;
static const float ImpostorAlphaCut = 0.5f;

struct ImpostorVSOutput
{
    [[vk::ext_decorate(18)]] float4 position_ : SV_POSITION;
    float3 posRel_ : TEXCOORD0;
    float2 uv_ : TEXCOORD1;
    nointerpolation float4 rotation_ : TEXCOORD2;
};

float2 OctEncode(float3 d)
{
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    float2 p = d.xz;
    if (d.y < 0.0f)
        p = (1.0f - abs(p.yx)) * float2(p.x >= 0.0f ? 1.0f : -1.0f, p.y >= 0.0f ? 1.0f : -1.0f);
    return p * 0.5f + 0.5f;
}

float3 OctDecode(float2 uv)
{
    const float2 p = uv * 2.0f - 1.0f;
    float3 d = float3(p.x, 1.0f - abs(p.x) - abs(p.y), p.y);
    if (d.y < 0.0f)
        d.xz = (1.0f - abs(d.zx)) * float2(d.x >= 0.0f ? 1.0f : -1.0f, d.z >= 0.0f ? 1.0f : -1.0f);
    return normalize(d);
}

void ImpostorAxes(float3 d, out float3 right, out float3 up)
{
    const float3 reference = abs(d.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
    right = normalize(cross(reference, d));
    up = cross(d, right);
}

uint2 ImpostorCell(float3 d)
{
    return min(uint2(OctEncode(d) * float(ImpostorGrid)), uint2(ImpostorGrid - 1u, ImpostorGrid - 1u));
}

float3 ImpostorNormal(ScatterBatchGPUData batch, float4 rotation, float3 objectNormal)
{
    return normalize(mul((float3x3)batch.placed_.world_, ScatterRotate(rotation, objectNormal)));
}

#endif
