#ifndef BATAP_SHADOWS_HLSLI
#define BATAP_SHADOWS_HLSLI

#include "ShaderInterop.h"

[[vk::binding(ShadowsBinding, FrameSet)]]
StructuredBuffer<ShadowGPUData> ShadowBuffer;

[[vk::binding(ShadowSamplerBinding, BindlessSet)]] SamplerComparisonState g_shadowSampler;
// The same descriptors as g_textures, typed for a depth compare: SampleCmp
// wants a single-channel texture.
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float> g_depthTextures[];

// Offset along the normal at a fully grazing incidence, counted in texels of
// the map
static const float ShadowNormalOffsetTexels = 2.0f;

// 1 where the light reaches P, 0 where a caster is in the way. Reading the
// matrix the pass rendered with is what keeps the two in step.
float ShadowMapVisibility(float3 P, float3 N, float3 L, float dL, uint shadowIndex)
{
    ShadowGPUData sh = ShadowBuffer[shadowIndex];

    // fix shadows acnee
    float NdotL = saturate(dot(N, L));
    P += N * (sh.texelWorld_ * dL * ShadowNormalOffsetTexels
              * sqrt(saturate(1.0f - NdotL * NdotL)));

    float4 clip = mul(sh.viewProj_, float4(P, 1.0f));
    if (clip.w <= 0.0f)
        return 1.0f;
    float3 ndc = clip.xyz / clip.w;

    // Outside the view is not "shadowed", it is "unknown": the tile holds no
    // caster for this direction, and a neighbouring tile would answer for a
    // place it never saw.
    if (any(abs(ndc.xy) > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f)
        return 1.0f;

    // The pass draws through setViewportYUpRect, whose negative height flips Y.
    float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    uv = uv * sh.uvScale_ + float2(sh.uvOffset_[0], sh.uvOffset_[1]);

    // PCF 3x3
    float sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
            sum += g_depthTextures[sh.atlasTexture_].SampleCmpLevelZero(
                g_shadowSampler, uv + float2(x, y) * sh.texelUV_, ndc.z);
    }
    return sum / 9.0f;
}

#endif
