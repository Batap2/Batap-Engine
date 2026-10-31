#ifndef BATAP_CAMERA_HLSLI
#define BATAP_CAMERA_HLSLI

#include "ShaderInterop.h"

bool CameraOrtho(CameraGPUData cam)
{
    return cam.orthoHeight_ > 0.0f;
}

void CameraRay(CameraGPUData cam, float2 ndc, out float3 origin, out float3 dir)
{
    const float3 across = (ndc.x / cam.proj_[0][0]) * cam.right_ +
                          (ndc.y / cam.proj_[1][1]) * cam.up_;
    origin = CameraOrtho(cam) ? across : float3(0.0f, 0.0f, 0.0f);
    dir = CameraOrtho(cam) ? cam.fwd_ : normalize(across + cam.fwd_);
}

float3 CameraToEye(CameraGPUData cam, float3 posRel)
{
    return CameraOrtho(cam) ? -cam.fwd_ : normalize(-posRel);
}

#endif
