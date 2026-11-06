#include "ShaderInterop.h"
#include "Sky.hlsli"

[[vk::binding(SamplerBinding, BindlessSet)]]  SamplerState      g_sampler;
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];

[[vk::push_constant]] DrawPush g_draw;

float2 Hammersley(uint i, uint count)
{
    return float2(float(i) / float(count), float(reversebits(i)) * 2.3283064365386963e-10f);
}

float4 main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0) : SV_Target
{
    float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    float3 N = SkyEquirectDir(uv);
    float3 up = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    float a = g_draw.skyRoughness_ * g_draw.skyRoughness_;
    float a2 = a * a;

    float3 sum = 0.0f.xxx;
    float weight = 0.0f;
    for (uint i = 0u; i < g_draw.skySamples_; ++i)
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
    return float4(sum / max(weight, 1e-4f), 1.0f);
}
