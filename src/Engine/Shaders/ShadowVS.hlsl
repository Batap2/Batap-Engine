#include "ShaderInterop.h"

[[vk::binding(InstancesBinding, FrameSet)]]
StructuredBuffer<StaticMeshGPUData> StaticMeshInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

float4 main(float3 position_ : POSITION) : SV_POSITION
{
    StaticMeshGPUData inst = StaticMeshInstancebuffer[g_draw.instanceIndex_];
    return mul(g_draw.shadowViewProj_, mul(inst.world_, float4(position_, 1.0f)));
}
