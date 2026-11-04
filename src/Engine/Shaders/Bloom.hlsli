#ifndef BATAP_BLOOM_HLSLI
#define BATAP_BLOOM_HLSLI

#include "ShaderInterop.h"

[[vk::binding(SamplerBinding, BindlessSet)]] SamplerState g_sampler;
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];

[[vk::push_constant]] DrawPush g_draw;

struct BloomInput
{
    float4 svpos_ : SV_POSITION;
    float2 ndc_ : TEXCOORD0;
};

float2 BloomUv(float2 ndc)
{
    return float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
}

float2 BloomTexel()
{
    uint width;
    uint height;
    g_textures[g_draw.bloomSource_].GetDimensions(width, height);
    return 1.0f / float2(width, height);
}

float3 BloomFetch(float2 uv, float2 texel)
{
    // The only sampler repeats: unclamped, the sun at one edge blooms on the opposite one.
    const float2 at = clamp(uv, 0.5f * texel, 1.0f - 0.5f * texel);
    return max(g_textures[g_draw.bloomSource_].SampleLevel(g_sampler, at, 0.0f).rgb, 0.0f);
}

#endif
