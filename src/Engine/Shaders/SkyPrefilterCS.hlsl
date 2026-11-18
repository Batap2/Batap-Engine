#include "ShaderInterop.h"
#include "Sky.hlsli"
#include "ScreenCompute.hlsli"

[[vk::binding(SamplerBinding, BindlessSet)]]  SamplerState      g_sampler;
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];

[[vk::push_constant]] DrawPush g_draw;

static const uint SkyPrefilterLanes = 16u;
groupshared float4 g_partial[SkyPrefilterTexelsPerGroup][SkyPrefilterLanes];

float2 Hammersley(uint i, uint count)
{
    return float2(float(i) / float(count), float(reversebits(i)) * 2.3283064365386963e-10f);
}

// A thread alone waits on its fetches one after the other, and the small levels have too few
// texels to hide it: SkyPrefilterLanes threads per texel, each taking every SkyPrefilterLanes-th
// sample.
[numthreads(SkyPrefilterLanes, SkyPrefilterTexelsPerGroup, 1)]
void main(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID)
{
    const uint lane = local.x;
    const uint row = local.y;
    const uint2 texel = uint2(group.x * SkyPrefilterTexelsPerGroup + row, group.y);
    const uint2 size = ScreenImageSize(g_draw.storageImage_);
    const float2 ndc = ScreenNdc(texel, size);
    float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    float3 N = SkyEquirectDir(uv);
    float3 up = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    float a = g_draw.skyRoughness_ * g_draw.skyRoughness_;
    float a2 = a * a;

    float3 sum = 0.0f.xxx;
    float weight = 0.0f;
    const uint samples = texel.x < size.x ? g_draw.skySamples_ : 0u;
    for (uint i = lane; i < samples; i += SkyPrefilterLanes)
    {
        float2 xi = Hammersley(i, g_draw.skySamples_);
        float phi = 2.0f * SKY_PI * xi.x;
        float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a2 - 1.0f) * xi.y));
        float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
        float3 H = T * (sinTheta * cos(phi)) + B * (sinTheta * sin(phi)) + N * cosTheta;
        float3 L = 2.0f * dot(N, H) * H - N;
        float NdotL = dot(N, L);
        if (NdotL <= 0.0f)
            continue;
        sum += g_textures[g_draw.skySource_].SampleLevel(g_sampler, SkyEquirectUv(L), 0.0f).rgb *
               NdotL;
        weight += NdotL;
    }

    g_partial[row][lane] = float4(sum, weight);
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint stride = SkyPrefilterLanes / 2u; stride > 0u; stride >>= 1u)
    {
        if (lane < stride)
            g_partial[row][lane] += g_partial[row][lane + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0u && texel.x < size.x)
        g_storageImages[g_draw.storageImage_][texel] =
            float4(g_partial[row][0].rgb / max(g_partial[row][0].a, 1e-4f), 1.0f);
}
