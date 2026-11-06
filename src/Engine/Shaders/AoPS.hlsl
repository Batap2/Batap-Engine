#include "GiCache.hlsli"
#include "ShaderInterop.h"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::binding(FrameConstantsBinding, FrameSet)]] StructuredBuffer<FrameGPUData> FrameBuffer;
[[vk::binding(CamerasBinding, FrameSet)]] StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::push_constant]] DrawPush g_draw;

static const float AoPi = 3.14159265358979f;
static const uint AoSlices = 1u;
static const uint AoSteps = 8u;
static const uint AoSectors = 32u;
static const float AoThicknessPixels = 2.0f;
static const float AoMaxRadiusOfHeight = 0.25f;
static const float AoAngleScale = 1.05f;
static const float AoSlopeTolerance = 0.5f;

struct AoScreen
{
    float2 size_;
    float2 invProj_;
    float znear_;
};

float3 AoViewPosition(AoScreen v, float2 pixel, float linearDepth)
{
    const float2 ndc =
        float2(pixel.x / v.size_.x * 2.0f - 1.0f, 1.0f - pixel.y / v.size_.y * 2.0f);
    return float3(ndc * v.invProj_ * linearDepth, -linearDepth);
}

float AoNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

uint AoSectorMask(float2 range)
{
    const uint a = uint(floor(saturate(range.x) * AoSectors));
    const uint b = uint(ceil(saturate(range.y) * AoSectors));
    if (b <= a)
        return 0u;
    const uint count = b - a;
    return (count >= 32u ? 0xFFFFFFFFu : (1u << count) - 1u) << a;
}

float AoAcos(float x)
{
    const float ax = abs(x);
    const float r = (-0.156583f * ax + AoPi * 0.5f) * sqrt(1.0f - ax);
    return x >= 0.0f ? r : AoPi - r;
}

// Antiderivative of cos(phi) * sin(phi + n): the cosine-weighted solid angle
// of a slice around V, phi measured from the projected normal at angle n.
float AoPrimitive(float phi, float n, float sinN)
{
    return -0.25f * cos(2.0f * phi + n) + 0.5f * phi * sinN;
}

float AoIntegral(float a, float b, float n, float sinN)
{
    const float split = clamp(-n, a, b);
    const float atSplit = AoPrimitive(split, n, sinN);
    return abs(atSplit - AoPrimitive(a, n, sinN)) + abs(AoPrimitive(b, n, sinN) - atSplit);
}

float AoSectorEdge(uint k)
{
    return (float(k) / float(AoSectors) - 0.5f) * (AoPi / AoAngleScale);
}

bool AoClipToSphere(float3 P, float3 dir, float2 span, float radius, out float2 clipped)
{
    const float b = dot(dir, P);
    const float disc = b * b - dot(P, P) + radius * radius;
    clipped = span;
    if (disc <= 0.0f)
        return false;
    const float h = sqrt(disc);
    clipped = float2(max(span.x, b - h), min(span.y, b + h));
    return clipped.x < clipped.y;
}

float main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData frame = FrameBuffer[0];
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const Texture2D<float4> depths = g_textures[frame.depthTexture_];
    const float depth = depths.Load(int3(int2(svpos.xy), 0)).r;
    if (depth <= 0.0f)
        return 1.0f;

    AoScreen v;
    uint width;
    uint height;
    depths.GetDimensions(width, height);
    v.size_ = float2(width, height);
    v.invProj_ = 1.0f / float2(cam.proj_[0][0], cam.proj_[1][1]);
    v.znear_ = cam.znear_;

    const float L = v.znear_ / depth;
    const float3 P = AoViewPosition(v, svpos.xy, L);
    const float3 N = normalize(
        g_textures[frame.viewNormalTexture_].Load(int3(int2(svpos.xy), 0)).xyz * 2.0f - 1.0f);
    const float3 V = normalize(-P);

    const float radius = frame.aoRadius_ > 0.0f
                             ? frame.aoRadius_
                             : GiCellBase * max(length(P) / GiLevelDistance, 1.0f);
    const float pixelsPerMetreAtUnit = cam.proj_[1][1] * v.size_.y * 0.5f;
    const float radiusPixels =
        min(radius * pixelsPerMetreAtUnit / L, AoMaxRadiusOfHeight * v.size_.y);
    if (radiusPixels < 2.0f)
        return 1.0f;

    const float noiseSlice = AoNoise(svpos.xy);
    const float noiseStep = AoNoise(svpos.xy + float2(5.588238f, 5.588238f));

    float visible = 0.0f;
    float total = 0.0f;
    [unroll]
    for (uint slice = 0u; slice < AoSlices; ++slice)
    {
        const float phi = AoPi * (float(slice) + noiseSlice) / float(AoSlices);
        const float2 dir = float2(cos(phi), sin(phi));

        const float3 along = AoViewPosition(v, svpos.xy + dir, L) - P;
        const float3 ortho = normalize(along - dot(along, V) * V);
        const float3 axis = cross(V, ortho);
        const float3 projN = N - axis * dot(N, axis);
        const float projLength = length(projN);
        if (projLength < 1e-4f)
            continue;
        const float n = (dot(projN, ortho) < 0.0f ? -1.0f : 1.0f) *
                        acos(clamp(dot(projN, V) / projLength, 0.0f, 1.0f));

        uint occluded = 0u;
        [unroll]
        for (uint side = 0u; side < 2u; ++side)
        {
            const float sideSign = side == 0u ? 1.0f : -1.0f;
            float prevDepth = depth;
            float prevT = 0.0f;
            float prevL = L;
            float2 prevSlopes = float2(0.0f, 0.0f);
            [loop]
            for (uint j = 0u; j < AoSteps; ++j)
            {
                const float s = (float(j) + noiseStep) / float(AoSteps);
                const float t = 1.0f + s * s * (radiusPixels - 1.0f);
                const float2 at = svpos.xy + sideSign * dir * t;
                if (any(at < 0.0f) || any(at >= v.size_))
                    break;
                const float2 texel = floor(at) + 0.5f;
                const float sampleDepth = depths.Load(int3(int2(texel), 0)).r;

                // The reversed-Z buffer is linear in screen space across a
                // plane: a steady slope means the same surface as the steps
                // before, and the step covers the depth since the last one.
                const float slope = (sampleDepth - prevDepth) / (t - prevT);
                const bool connected =
                    j >= 2u && sampleDepth > 0.0f && prevDepth > 0.0f &&
                    all(abs(slope - prevSlopes) <= AoSlopeTolerance * abs(prevSlopes));
                prevSlopes =
                    sampleDepth > 0.0f ? float2(slope, prevSlopes.x) : float2(0.0f, 0.0f);
                prevDepth = sampleDepth;
                prevT = t;
                if (sampleDepth <= 0.0f)
                    continue;

                const float Ls = v.znear_ / sampleDepth;
                const float minThickness =
                    frame.aoThickness_ + AoThicknessPixels * Ls / pixelsPerMetreAtUnit;
                const float thickness = max(connected ? abs(Ls - prevL) : 0.0f, minThickness);
                prevL = Ls;

                const float3 S = AoViewPosition(v, texel, Ls);
                const float sDistance = length(S);
                const float3 ray = S / sDistance;
                float2 span;
                if (!AoClipToSphere(P, ray, float2(sDistance, sDistance * (Ls + thickness) / Ls),
                                    radius, span))
                    continue;

                const float2 cosines = float2(dot(normalize(ray * span.x - P), V),
                                              dot(normalize(ray * span.y - P), V));
                const float2 theta = sideSign * float2(AoAcos(clamp(cosines.x, -1.0f, 1.0f)),
                                                       AoAcos(clamp(cosines.y, -1.0f, 1.0f)));
                const float2 range = (theta - n) * (AoAngleScale / AoPi) + 0.5f;
                occluded |= AoSectorMask(float2(min(range.x, range.y), max(range.x, range.y)));
            }
        }

        const float sinN = sin(n);
        const float sliceTotal = AoIntegral(-0.5f * AoPi, 0.5f * AoPi, n, sinN);
        float sliceOccluded = 0.0f;
        uint runs = occluded;
        [loop]
        while (runs != 0u)
        {
            const uint first = firstbitlow(runs);
            const uint after = ~runs & (0xFFFFFFFFu << first);
            const uint end = after != 0u ? firstbitlow(after) : AoSectors;
            sliceOccluded += AoIntegral(AoSectorEdge(first), AoSectorEdge(end), n, sinN);
            runs = end < AoSectors ? runs & (0xFFFFFFFFu << end) : 0u;
        }
        visible += projLength * max(sliceTotal - sliceOccluded, 0.0f);
        total += projLength * sliceTotal;
    }

    const float ao = total > 0.0f ? visible / total : 1.0f;
    return lerp(1.0f, ao, frame.aoStrength_);
}
