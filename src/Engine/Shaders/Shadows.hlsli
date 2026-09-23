#ifndef BATAP_SHADOWS_HLSLI
#define BATAP_SHADOWS_HLSLI

#include "ShaderInterop.h"

[[vk::binding(ShadowsBinding, FrameSet)]]
StructuredBuffer<ShadowGPUData> ShadowBuffer;

[[vk::binding(ShadowSamplerBinding, BindlessSet)]] SamplerComparisonState g_shadowSampler;
// The same descriptors as g_textures, typed for a depth compare: SampleCmp
// wants a single-channel texture.
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float> g_depthTextures[];

// 1 where the light reaches P, 0 where a caster is in the way. Reading the
// matrix the pass rendered with is what keeps the two in step.
float ShadowMapVisibility(float3 P, uint shadowIndex)
{
    ShadowGPUData sh = ShadowBuffer[shadowIndex];

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

    return g_depthTextures[sh.atlasTexture_].SampleCmpLevelZero(g_shadowSampler, uv, ndc.z);
}

#endif
