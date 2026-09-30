#ifndef BATAP_SHADOWS_HLSLI
#define BATAP_SHADOWS_HLSLI

#include "ShaderInterop.h"

[[vk::binding(ShadowsBinding, FrameSet)]]
StructuredBuffer<ShadowGPUData> ShadowBuffer;

// Same descriptors as g_textures, typed single-channel for the depth atlases.
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float> g_depthTextures[];

// Must match the pass's face order: +X, -X, +Y, -Y, +Z, -Z.
uint CubeFaceIndex(float3 d)
{
    float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)
        return d.x > 0.0f ? 0u : 1u;
    if (a.y >= a.z)
        return d.y > 0.0f ? 2u : 3u;
    return d.z > 0.0f ? 4u : 5u;
}

// Back faces are stored, so lit surfaces need no bias; but where a caster rests
// in a receiver its back face runs below the receiver's plane, deeper by up to
// the receiver's slope per map texel, and light leaks along the contact. Only
// receivers already in shadow are pushed: pushed everywhere, every lit
// silhouette darkens against its own back face right behind it.
static const float ShadowContactTexels = 1.5f;

// A receiver in a caster's shadow has half its kernel shadowed; a lit face
// with a thin caster crossing its kernel, less.
static const float ShadowContactTaps = 2.0f;

// Texels are compared with the receiver's plane, not with P: the kernel reaches
// two texels out, and along an edge the light runs along, the object's own back
// face is nearer than P there, which combs the edge. The plane comes from the
// vertex normal: screen derivatives straddle silhouettes. A grazed plane has no
// usable slope; 16 keeps a ground exact down to a sun 3.6 degrees up.
static const float ShadowPlaneMaxSlope = 16.0f;

// A back face flush with the receiver's plane (a crate's bottom on the ground)
// ties on float noise, which opens the push along the lit foot. Ties are lit.
static const float ShadowTieTexels = 0.1f;

// The kernel is nine bilinear taps one texel apart, as their 4x4 texel
// footprint with the same weights: fetched once, compared per texel.
float SampleShadowView(ShadowGPUData sh, float3 P, float3 N, float3 L, float texelAtP)
{
    float4 clip = mul(sh.viewProj_, float4(P, 1.0f));
    if (clip.w <= 0.0f)
        return 1.0f;
    float3 ndc = clip.xyz / clip.w;

    if (any(abs(ndc.xy) > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f)
        return 1.0f;

    // The pass draws through setViewportYUpRect, whose negative height flips Y.
    float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    uv = uv * sh.uvScale_ + float2(sh.uvOffset_[0], sh.uvOffset_[1]);

    // A plane maps to a plane, so any step is exact; a longer one stays clear
    // of float noise in ndc depth.
    const float probe = 4.0f;
    float step = probe * texelAtP;

    // One texel along L, in ndc depth.
    float4 back = mul(sh.viewProj_, float4(P - L * step, 1.0f));
    float zTexel = max((back.z / back.w - ndc.z) / probe, 0.0f);

    // Receiver plane slope in ndc depth per map texel, +y down the map.
    float3 T1 = normalize(cross(N, abs(N.x) < 0.9f ? float3(1.0f, 0.0f, 0.0f)
                                                   : float3(0.0f, 1.0f, 0.0f)));
    float3 T2 = cross(N, T1);
    float4 c1 = mul(sh.viewProj_, float4(P + T1 * step, 1.0f));
    float4 c2 = mul(sh.viewProj_, float4(P + T2 * step, 1.0f));
    float2 slope = float2(0.0f, 0.0f);
    if (c1.w > 0.0f && c2.w > 0.0f)
    {
        float3 d1 = c1.xyz / c1.w - ndc;
        float3 d2 = c2.xyz / c2.w - ndc;
        float det = d1.x * d2.y - d1.y * d2.x;
        if (abs(det) > 1e-20f)
        {
            float2 perNdc = float2(d1.z * d2.y - d2.z * d1.y, d1.x * d2.z - d2.x * d1.z) / det;
            slope = float2(perNdc.x, -perNdc.y) * (2.0f * sh.texelUV_ / sh.uvScale_);
            // Bounded in length, never per component: that would turn the
            // gradient, and a grazed receiver would read a slope along its edge.
            float maxSlope = ShadowPlaneMaxSlope * zTexel;
            float len = length(slope);
            if (len > maxSlope)
                slope *= maxSlope / len;
        }
    }

    float atlasSize = 1.0f / sh.texelUV_;
    int2 tileMin = int2(round(float2(sh.uvOffset_[0], sh.uvOffset_[1]) * atlasSize));
    int2 tileMax = tileMin + int(round(sh.uvScale_ * atlasSize)) - 1;

    float2 p = uv * atlasSize - 0.5f;
    float2 base = floor(p);
    float2 f = p - base;
    float wx[4] = {1.0f - f.x, 1.0f, 1.0f, f.x};
    float wy[4] = {1.0f - f.y, 1.0f, 1.0f, f.y};

    float zPush = ShadowContactTexels * max(zTexel, length(slope));
    float zTie = ShadowTieTexels * zTexel;
    float lit = 0.0f;
    float pushed = 0.0f;
    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            int2 t = clamp(int2(base) + int2(i - 1, j - 1), tileMin, tileMax);
            float d = g_depthTextures[sh.atlasTexture_].Load(int3(t, 0));
            float z = ndc.z + dot(slope, float2(t) - p);
            float w = wx[i] * wy[j];
            lit += w * (min(z - zTie, 1.0f) <= d ? 1.0f : 0.0f);
            pushed += w * (min(z + zPush, 1.0f) <= d ? 1.0f : 0.0f);
        }
    }
    float gate = saturate(10.0f - ShadowContactTaps - lit);
    lit = lerp(lit, pushed, gate);
    return lit / 9.0f;
}

float ShadowMapVisibility(float3 P, float3 N, float3 L, float dL, uint shadowIndex,
                          out float coverage)
{
    ShadowGPUData sh = ShadowBuffer[shadowIndex];
    coverage = saturate(sh.strength_);
    if (coverage <= 0.0f)
        return 1.0f;
    return SampleShadowView(sh, P, N, L, sh.texelWorld_ * dL);
}

// The first sphere holding P, not a view-depth split: the fit is spherical.
// The pass leaves entries it did not write at strength_ 0.
uint CascadeIndexAt(float3 P, uint firstView)
{
    [loop]
    for (uint c = 0; c < ShadowCascadeCount; ++c)
    {
        ShadowGPUData sh = ShadowBuffer[firstView + c];
        if (sh.strength_ > 0.0f && distance(P, sh.sphere_.xyz) <= sh.sphere_.w)
            return c;
    }
    return ShadowCascadeCount;
}

// Across a cascade boundary the texel jumps, and an edge rasterised on both
// sides shows the step as a line; fading over this band makes it a ramp.
static const float CascadeFadeBand = 0.1f;

float CascadeFadeAt(float3 P, ShadowGPUData sh)
{
    float d = distance(P, sh.sphere_.xyz) / sh.sphere_.w;
    return saturate((d - (1.0f - CascadeFadeBand)) / CascadeFadeBand);
}

// A point the next sphere does not hold keeps its cascade whole: fading toward
// "outside the map", which reads as lit, would leak light.
uint CascadeNextAt(float3 P, uint firstView, uint c)
{
    if (c + 1 >= ShadowCascadeCount)
        return ShadowCascadeCount;
    ShadowGPUData next = ShadowBuffer[firstView + c + 1];
    if (next.strength_ <= 0.0f || distance(P, next.sphere_.xyz) > next.sphere_.w)
        return ShadowCascadeCount;
    return c + 1;
}

// dTexel: the distance to a positional light (its cascades are perspective
// views from it), 1 under a directional one.
float CascadeVisibility(float3 P, float3 N, float3 L, float dTexel, uint firstView,
                        out float coverage)
{
    uint c = CascadeIndexAt(P, firstView);
    coverage = c < ShadowCascadeCount ? 1.0f : 0.0f;
    if (c >= ShadowCascadeCount)
        return 1.0f;
    ShadowGPUData sh = ShadowBuffer[firstView + c];
    float vis = SampleShadowView(sh, P, N, L, sh.texelWorld_ * dTexel);

    float t = CascadeFadeAt(P, sh);
    if (t > 0.0f)
    {
        uint n = CascadeNextAt(P, firstView, c);
        if (n < ShadowCascadeCount)
        {
            ShadowGPUData next = ShadowBuffer[firstView + n];
            vis = lerp(vis, SampleShadowView(next, P, N, L, next.texelWorld_ * dTexel), t);
        }
    }
    return vis;
}

float3 CascadeDebugTint(uint c)
{
    const float3 tints[4] = {float3(1.0f, 0.3f, 0.3f), float3(0.3f, 1.0f, 0.3f),
                             float3(0.35f, 0.5f, 1.0f), float3(1.0f, 0.9f, 0.3f)};
    return tints[min(c, 3u)];
}

float3 CascadeDebugTintAt(float3 P, uint firstView)
{
    uint c = CascadeIndexAt(P, firstView);
    if (c >= ShadowCascadeCount)
        return float3(0.0f, 0.0f, 0.0f);
    float3 tint = CascadeDebugTint(c);
    float t = CascadeFadeAt(P, ShadowBuffer[firstView + c]);
    uint n = CascadeNextAt(P, firstView, c);
    if (t > 0.0f && n < ShadowCascadeCount)
        tint = lerp(tint, CascadeDebugTint(n), t);
    return tint;
}

#endif
