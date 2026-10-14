#include "ShaderInterop.h"
#include "Shadows.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::binding(InstancesBinding, FrameSet)]]
StructuredBuffer<StaticMeshGPUData> StaticMeshInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

// Relative to the render camera, as the light matrices are built.
float4 main(float3 position_ : POSITION) : SV_POSITION
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const StaticMeshGPUData inst = StaticMeshInstancebuffer[g_draw.instanceIndex_];
    const float3 posRel =
        (inst.world_._m03_m13_m23 - cam.pos_) + mul((float3x3)inst.world_, position_);
    return mul(ShadowBuffer[g_draw.shadowViewIndex_].viewProj_, float4(posRel, 1.0f));
}
