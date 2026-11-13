#include "ShaderInterop.h"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::binding(StorageImagesBinding, BindlessSet)]] [[vk::image_format("unknown")]]
RWTexture2D<float4> g_storageImages[];
[[vk::binding(FrameConstantsBinding, FrameSet)]] StructuredBuffer<FrameGPUData> FrameBuffer;
[[vk::binding(CamerasBinding, FrameSet)]] StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::push_constant]] DrawPush g_draw;

static const uint ScreenShadowHardSamples = 4u;
static const uint ScreenShadowFadeSamples = 8u;
static const uint ScreenShadowReads = ScreenShadowSamples / ScreenShadowWave + 2u;
static const float ScreenShadowContrast = 4.0f;
static const float ScreenShadowMinThickness = 0.01f;
static const float ScreenShadowThickness = 0.5f;
static const float ScreenShadowEdge = 0.02f;

groupshared float g_depths[ScreenShadowReads * ScreenShadowWave];
groupshared uint g_skipGroup;

float ScreenShadowDepth(Texture2D<float4> depths, int2 pixel, int2 size)
{
    if (any(pixel < 0) || any(pixel >= size))
        return 0.0f;
    return depths.Load(int3(pixel, 0)).r;
}

[numthreads(ScreenShadowWave, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint thread : SV_GroupIndex)
{
    const FrameGPUData frame = FrameBuffer[0];
    const float4 light = frame.screenShadowLightCoord_;
    const Texture2D<float4> depths = g_textures[frame.depthTexture_];
    uint width;
    uint height;
    depths.GetDimensions(width, height);
    const int2 size = int2(width, height);

    int2 xy = int2(groupId.yz) * int(ScreenShadowWave) +
              frame.screenShadowWaves_[g_draw.instanceIndex_].xy;
    const float2 lightXY = floor(light.xy) + 0.5f;
    const float2 lightFraction = light.xy - lightXY;
    const bool reverse = light.w > 0.0f;
    const int2 signXY = sign(xy);
    const bool horizontal = abs(xy.x + signXY.y) < abs(xy.y - signXY.x);
    const int2 axis = horizontal ? int2(signXY.y, 0) : int2(0, -signXY.x);
    xy = axis * int(groupId.x) + xy;
    const float2 xyf = float2(xy);
    const bool xMajor = abs(xyf.x) > abs(xyf.y);
    const float major = xMajor ? xyf.x : xyf.y;
    const float majorStart = abs(major);
    const float majorEnd = abs(major) - float(ScreenShadowWave);
    float majorFraction = xMajor ? lightFraction.x : lightFraction.y;
    majorFraction = major > 0.0f ? -majorFraction : majorFraction;
    const float2 startXY = xyf + lightXY;
    const float2 endXY = lerp(light.xy, startXY,
                              (majorEnd + majorFraction) / (majorStart + majorFraction));
    const float2 delta = startXY - endXY;
    const float threadStep = float(thread ^ (reverse ? 0u : ScreenShadowWave - 1u));
    float2 pixelXY = lerp(startXY, endXY, threadStep / float(ScreenShadowWave));
    const float pixelDistance = majorStart - threadStep + majorFraction;

    const float direction = -light.w;
    const int2 writeXY = int2(floor(pixelXY));
    float samplingDepth[ScreenShadowReads];
    float sampleDistance[ScreenShadowReads];
    [unroll] for (uint i = 0u; i < ScreenShadowReads; ++i)
    {
        const int2 readXY = int2(floor(pixelXY));
        const float minor = xMajor ? pixelXY.y : pixelXY.x;
        const float across = frac(minor) - 0.5f;
        const int bias = across > 0.0f ? 1 : -1;
        const int2 neighbourXY = readXY + (xMajor ? int2(0, bias) : int2(bias, 0));
        const float nearDepth = ScreenShadowDepth(depths, readXY, size);
        const float otherDepth = ScreenShadowDepth(depths, neighbourXY, size);
        const bool edge = abs(nearDepth - otherDepth) > nearDepth * ScreenShadowEdge;
        samplingDepth[i] = edge ? nearDepth : lerp(nearDepth, otherDepth, abs(across));
        sampleDistance[i] = pixelDistance + float(ScreenShadowWave * i) * direction;
        pixelXY += delta * direction;
    }

    const bool skip =
        samplingDepth[0] <= 0.0f || any(writeXY < 0) || any(writeXY >= size);
    if (thread == 0u)
        g_skipGroup = 1u;
    GroupMemoryBarrierWithGroupSync();
    if (!skip)
        g_skipGroup = 0u;
    GroupMemoryBarrierWithGroupSync();
    if (g_skipGroup != 0u)
        return;

    [unroll] for (uint r = 0u; r < ScreenShadowReads; ++r)
    {
        float stored = (samplingDepth[r] - light.z) / sampleDistance[r];
        if (r != 0u && sampleDistance[r] <= 0.0f)
            stored = 1e10f;
        g_depths[r * ScreenShadowWave + thread] = stored;
    }
    GroupMemoryBarrierWithGroupSync();
    if (skip)
        return;

    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const float thickness =
        0.5f * max(ScreenShadowMinThickness, ScreenShadowThickness * samplingDepth[0] / cam.znear_);
    const float depthScale = min(sampleDistance[0] + direction, 1.0f / thickness) *
                             sampleDistance[0] / samplingDepth[0];
    const float startDepth =
        (samplingDepth[0] - light.z) / sampleDistance[0] * depthScale + 1.0f;

    const uint first = thread + 1u;
    float hard = 1.0f;
    float4 soft = float4(1.0f, 1.0f, 1.0f, 1.0f);
    [unroll] for (uint h = 0u; h < ScreenShadowHardSamples; ++h)
        hard = min(hard, abs(startDepth - g_depths[first + h] * depthScale));
    [unroll] for (uint s = ScreenShadowHardSamples; s < ScreenShadowSamples - ScreenShadowFadeSamples;
                  ++s)
        soft[s & 3u] = min(soft[s & 3u], abs(startDepth - g_depths[first + s] * depthScale));
    [unroll] for (uint f = ScreenShadowSamples - ScreenShadowFadeSamples; f < ScreenShadowSamples;
                  ++f)
    {
        const float fade = float(f + 1u - (ScreenShadowSamples - ScreenShadowFadeSamples)) /
                           float(ScreenShadowFadeSamples + 1u) * 0.75f;
        soft[f & 3u] = min(soft[f & 3u], abs(startDepth - g_depths[first + f] * depthScale) + fade);
    }

    soft = saturate(soft * ScreenShadowContrast + (1.0f - ScreenShadowContrast));
    hard = saturate(hard * ScreenShadowContrast + (1.0f - ScreenShadowContrast));
    g_storageImages[g_draw.submeshIndex_][writeXY] = min(hard, dot(soft, 0.25f)).xxxx;
}
