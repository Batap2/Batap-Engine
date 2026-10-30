#include "ShaderInterop.h"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

struct PS_INPUT
{
    float4 position_ : SV_POSITION;
    float3 posRel_   : TEXCOORD0;
    float3 nrmWS_    : TEXCOORD1;
};

float4 main(PS_INPUT input) : SV_Target
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const float3 normal = normalize(mul((float3x3)cam.view_, input.nrmWS_));
    return float4(normal * 0.5f + 0.5f, 1.0f);
}
