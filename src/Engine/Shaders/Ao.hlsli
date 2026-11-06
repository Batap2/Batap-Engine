#ifndef BATAP_AO_HLSLI
#define BATAP_AO_HLSLI

#include "ShaderInterop.h"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::binding(FrameConstantsBinding, FrameSet)]] StructuredBuffer<FrameGPUData> FrameBuffer;
[[vk::binding(CamerasBinding, FrameSet)]] StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::push_constant]] DrawPush g_draw;

struct AoScreen
{
    float2 size_;
    float2 invProj_;
    float znear_;
};

AoScreen AoScreenOf(Texture2D<float4> depths, CameraGPUData cam)
{
    AoScreen v;
    uint width;
    uint height;
    depths.GetDimensions(width, height);
    v.size_ = float2(width, height);
    v.invProj_ = 1.0f / float2(cam.proj_[0][0], cam.proj_[1][1]);
    v.znear_ = cam.znear_;
    return v;
}

float3 AoViewPosition(AoScreen v, float2 pixel, float linearDepth)
{
    const float2 ndc =
        float2(pixel.x / v.size_.x * 2.0f - 1.0f, 1.0f - pixel.y / v.size_.y * 2.0f);
    return float3(ndc * v.invProj_ * linearDepth, -linearDepth);
}

float3 AoViewNormal(int2 pixel)
{
    return normalize(
        g_textures[FrameBuffer[0].viewNormalTexture_].Load(int3(pixel, 0)).xyz * 2.0f - 1.0f);
}

// The AO runs at half resolution: a half-resolution texel stands for the
// top-left full-resolution pixel of its 2x2 block.
int2 AoFullTexel(int2 half)
{
    return half * 2;
}

static const float AoNormalPower = 8.0f;
static const float AoPlaneTolerance = 0.02f;

float AoSurfaceWeight(float3 P, float3 N, float3 Q, float3 Nq)
{
    const float tolerance = AoPlaneTolerance * -P.z;
    return saturate(1.0f - abs(dot(Q - P, N)) / tolerance) *
           pow(saturate(dot(N, Nq)), AoNormalPower);
}

#endif
