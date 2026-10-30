#ifndef BATAP_MOTION_HLSLI
#define BATAP_MOTION_HLSLI

#include "ShaderInterop.h"

float4x4 RelativeTo(float4x4 world, float3 origin)
{
    world._m03_m13_m23 -= origin;
    return world;
}

float3x3 Inverse(float3x3 m)
{
    const float3 c0 = cross(m[1], m[2]);
    const float3 c1 = cross(m[2], m[0]);
    const float3 c2 = cross(m[0], m[1]);
    return transpose(float3x3(c0, c1, c2)) / dot(m[0], c0);
}

float3 PreviousPosition(float3 posRel, float4x4 placed, float4x4 prevPlaced)
{
    const float3 local = mul(Inverse((float3x3)placed), posRel - placed._m03_m13_m23);
    return mul(prevPlaced, float4(local, 1.0f)).xyz;
}

float2 MotionNdc(CameraGPUData cam, float3 posRel, float3 prevRel)
{
    const float4 clip = mul(cam.proj_, float4(mul((float3x3)cam.view_, posRel), 1.0f));
    const float4 prevClip = mul(cam.prevViewProj_, float4(prevRel, 1.0f));
    return prevClip.xy / prevClip.w - clip.xy / clip.w;
}

float3 MotionDebugColor(float2 motionNdc)
{
    return float3(saturate(0.5f + 8.0f * motionNdc), 0.5f);
}

#endif
