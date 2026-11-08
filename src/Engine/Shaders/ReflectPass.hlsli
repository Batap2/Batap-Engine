#ifndef BATAP_REFLECT_PASS_HLSLI
#define BATAP_REFLECT_PASS_HLSLI

#include "Brdf.hlsli"
#include "Camera.hlsli"
#include "GiCache.hlsli"
#include "Reflections.hlsli"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::binding(FrameConstantsBinding, FrameSet)]] StructuredBuffer<FrameGPUData> FrameBuffer;
[[vk::binding(CamerasBinding, FrameSet)]] StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::push_constant]] DrawPush g_draw;

struct ReflectPixel
{
    float3 P_;
    float3 Ngeom_;
    float depth_;
    ReflectSurface surface_;
};

float2 ReflectScreenSize()
{
    uint width;
    uint height;
    g_textures[FrameBuffer[0].depthTexture_].GetDimensions(width, height);
    return float2(width, height);
}

float2 ReflectPixelNdc(float2 pixel, float2 size)
{
    const float2 ndc = pixel / size * 2.0f - 1.0f;
    return float2(ndc.x, -ndc.y);
}

float3 ReflectPixelRay(CameraGPUData cam, int2 pixel, float2 size)
{
    float3 origin;
    float3 dir;
    CameraRay(cam, ReflectPixelNdc(float2(pixel) + 0.5f, size), origin, dir);
    return dir;
}

float3 ReflectPosition(CameraGPUData cam, int2 pixel, float2 size, float depth)
{
    const float3 dir = ReflectPixelRay(cam, pixel, size);
    return dir * (cam.znear_ / max(depth, 1e-30f) / dot(dir, cam.fwd_));
}

bool ReflectLoadPixel(CameraGPUData cam, int2 pixel, float2 size, out ReflectPixel p)
{
    const FrameGPUData frame = FrameBuffer[0];
    p = (ReflectPixel)0;
    if (any(pixel < 0) || any(pixel >= int2(size)))
        return false;
    p.depth_ = g_textures[frame.depthTexture_].Load(int3(pixel, 0)).r;
    if (p.depth_ <= 0.0f)
        return false;
    p.P_ = ReflectPosition(cam, pixel, size, p.depth_);
    const float3 Nview = g_textures[frame.viewNormalTexture_].Load(int3(pixel, 0)).xyz * 2.0f - 1.0f;
    p.Ngeom_ = normalize(mul(Nview, (float3x3)cam.view_));
    p.surface_ = ReflectSurfaceDecode(g_uintTextures[frame.reflectSurfaceTexture_].Load(int3(pixel, 0)).xy);
    return true;
}

#endif
