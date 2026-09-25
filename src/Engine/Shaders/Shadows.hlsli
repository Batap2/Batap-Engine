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

// Face order as the pass lays the cube out: +X, -X, +Y, -Y, +Z, -Z.
uint CubeFaceIndex(float3 d)
{
    float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)
        return d.x > 0.0f ? 0u : 1u;
    if (a.y >= a.z)
        return d.y > 0.0f ? 2u : 3u;
    return d.z > 0.0f ? 4u : 5u;
}

// 1 where the light reaches P, 0 where a caster is in the way. Reading the
// matrix the pass rendered with is what keeps the two in step.
float ShadowMapVisibility(float3 P, float3 N, float3 L, float dL, uint shadowIndex)
{
    ShadowGPUData sh = ShadowBuffer[shadowIndex];
    if (sh.strength_ <= 0.0f)
        return 1.0f;

    // fix shadows acnee
    float NdotL = saturate(dot(N, L));
    P += N * (sh.texelWorld_ * dL * ShadowNormalOffsetTexels
              * sqrt(saturate(1.0f - NdotL * NdotL)));

    float4 clip = mul(sh.viewProj_, float4(P, 1.0f));
    if (clip.w <= 0.0f)
        return 1.0f;
    float3 ndc = clip.xyz / clip.w;

    // Outside the view is not "shadowed", it is "unknown". The slack is the
    // sideways travel the normal offset just applied: without it a receiver on
    // a cube edge drops out of the only face that covers it, and the seam lights up.
    float ndcTexel = 2.0f * sh.texelUV_ / sh.uvScale_;
    if (any(abs(ndc.xy) > 1.0f + ndcTexel * ShadowNormalOffsetTexels) || ndc.z < 0.0f ||
        ndc.z > 1.0f)
        return 1.0f;

    // The pass draws through setViewportYUpRect, whose negative height flips Y.
    float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    uv = uv * sh.uvScale_ + float2(sh.uvOffset_[0], sh.uvOffset_[1]);

    // PCF 3x3
    // One texel in from the border: the sampler filters over half a texel, so a
    // tap any closer would blend in the next tile, which is another face.
    float2 lo = float2(sh.uvOffset_[0], sh.uvOffset_[1]) + sh.texelUV_;
    float2 hi = lo + sh.uvScale_ - 2.0f * sh.texelUV_;

    float sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
            sum += g_depthTextures[sh.atlasTexture_].SampleCmpLevelZero(
                g_shadowSampler, clamp(uv + float2(x, y) * sh.texelUV_, lo, hi), ndc.z);
    }
    return lerp(1.0f, sum / 9.0f, sh.strength_);
}

#endif
