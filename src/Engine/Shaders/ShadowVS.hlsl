#include "ShaderInterop.h"
#include "Shadows.hlsli"

[[vk::binding(InstancesBinding, FrameSet)]]
StructuredBuffer<StaticMeshGPUData> StaticMeshInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

float4 main(float3 position_ : POSITION) : SV_POSITION
{
    StaticMeshGPUData inst = StaticMeshInstancebuffer[g_draw.instanceIndex_];
    float4 posWS = mul(inst.world_, float4(position_, 1.0f));
    return mul(ShadowBuffer[g_draw.shadowViewIndex_].viewProj_, posWS);
}
