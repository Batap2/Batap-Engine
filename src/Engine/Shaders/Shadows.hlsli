#ifndef BATAP_SHADOWS_HLSLI
#define BATAP_SHADOWS_HLSLI

#include "ShaderInterop.h"

[[vk::binding(ShadowsBinding, FrameSet)]]
StructuredBuffer<ShadowGPUData> ShadowBuffer;

// The same descriptors as g_textures, typed single-channel: the atlases are
// depth images, read texel by texel.
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float> g_depthTextures[];

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

// Receivers already in shadow are pushed this many texels away from the light,
// texels of depth or of the receiver's own slope over one texel of map when
// that is more. The pass stores back faces, so a lit surface meets the far
// side of its own object and needs no anti-acne bias; but where a caster rests
// in a receiver, its back face runs on below the receiver's plane, and without
// the push light leaks along the contact. It runs deeper by up to the
// receiver's slope per texel of map: under a grazing light one texel of map is
// many texels of ground. Pushed everywhere, the same near-equality darkens
// every lit edge instead: the lit side of a silhouette has its own back face
// right behind it. Along L it changes the compared depth only, never where the
// map is read.
static const float ShadowContactTexels = 1.5f;

// Taps' worth of shadow the kernel must hold before the push applies. A
// receiver in a caster's shadow has half its kernel shadowed; a lit face with
// a thin caster passing through its kernel, less.
static const float ShadowContactTaps = 2.0f;

// Each texel is compared with the receiver's plane where that texel's ray
// meets it, not with P: the kernel reads two texels away from P, and along an
// edge the light runs along, the object's own back face is nearer than P by
// its slope over that distance (1.6 texels of depth per texel of map on the
// top edge of the 60 ramp of test_shadow under its low lamp), which combs the
// edge. The plane comes from the vertex normal, never from screen derivatives,
// which straddle silhouettes. Its slope is bounded, in texels of depth per
// texel of map: a plane the light grazes has no usable one. The bound holds a
// ground exact under a sun 3.6 degrees up; lower is lit too little to show.
static const float ShadowPlaneMaxSlope = 16.0f;

// A back face flush with the receiver's plane, like the bottom of a crate on
// the ground, sits at the plane's own depth in every texel: a coin flip on
// float noise, which opens the push along the crate's lit foot. Ties are lit,
// by this margin.
static const float ShadowTieTexels = 0.1f;

// 1 where the light reaches P, 0 where a caster is in the way. Reading the
// matrix the pass rendered with is what keeps the two in step. texelAtP is the
// world size of one of the view's texels where P stands. The kernel is the 4x4
// texel footprint of nine bilinear taps one texel apart, with the same
// weights; fetched once, compared per texel.
float SampleShadowView(ShadowGPUData sh, float3 P, float3 N, float3 L, float texelAtP)
{
    float4 clip = mul(sh.viewProj_, float4(P, 1.0f));
    if (clip.w <= 0.0f)
        return 1.0f;
    float3 ndc = clip.xyz / clip.w;

    // Outside the view is not "shadowed", it is "unknown".
    if (any(abs(ndc.xy) > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f)
        return 1.0f;

    // The pass draws through setViewportYUpRect, whose negative height flips Y.
    float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    uv = uv * sh.uvScale_ + float2(sh.uvOffset_[0], sh.uvOffset_[1]);

    // Probes a few texels from P: a plane maps to a plane, so any step gives
    // the exact differences, and a longer one keeps them clear of float noise
    // in ndc depth, which is about a hundredth of a texel at 10 m.
    const float probe = 4.0f;
    float step = probe * texelAtP;

    // One texel along L in ndc depth: the push, the tie and the slope bound
    // count in it.
    float4 back = mul(sh.viewProj_, float4(P - L * step, 1.0f));
    float zTexel = max((back.z / back.w - ndc.z) / probe, 0.0f);

    // The receiver plane in ndc, in depth per map texel, +y down the map.
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
            // gradient, and a receiver the light grazes, whose plane is a
            // line in the map, would read a slope along its own edge.
            float maxSlope = ShadowPlaneMaxSlope * zTexel;
            float len = length(slope);
            if (len > maxSlope)
                slope *= maxSlope / len;
        }
    }

    float atlasSize = 1.0f / sh.texelUV_;
    int2 tileMin = int2(round(float2(sh.uvOffset_[0], sh.uvOffset_[1]) * atlasSize));
    int2 tileMax = tileMin + int(round(sh.uvScale_ * atlasSize)) - 1;

    // P in texel-centre space: texel k has its centre at k.
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
    return lerp(1.0f, lit / 9.0f, sh.strength_);
}

float ShadowMapVisibility(float3 P, float3 N, float3 L, float dL, uint shadowIndex)
{
    ShadowGPUData sh = ShadowBuffer[shadowIndex];
    if (sh.strength_ <= 0.0f)
        return 1.0f;
    return SampleShadowView(sh, P, N, L, sh.texelWorld_ * dL);
}

// The first sphere that holds P, not a split on view depth: the fit is
// spherical. Past the last one, the analytic occluders take over (R8).
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

// A cascade is a perspective view from the light too: its texel grows with the
// distance to the light, like a local view's.
float CascadeVisibility(float3 P, float3 N, float3 L, float dL, uint firstView)
{
    uint c = CascadeIndexAt(P, firstView);
    if (c >= ShadowCascadeCount)
        return 1.0f;
    ShadowGPUData sh = ShadowBuffer[firstView + c];
    return SampleShadowView(sh, P, N, L, sh.texelWorld_ * dL);
}

float3 CascadeDebugTint(uint c)
{
    const float3 tints[4] = {float3(1.0f, 0.3f, 0.3f), float3(0.3f, 1.0f, 0.3f),
                             float3(0.35f, 0.5f, 1.0f), float3(1.0f, 0.9f, 0.3f)};
    return tints[min(c, 3u)];
}

#endif
