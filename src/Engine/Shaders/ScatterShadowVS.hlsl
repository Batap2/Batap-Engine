#include "Scatter.hlsli"
#include "Shadows.hlsli"

[[vk::push_constant]] DrawPush g_draw;

float4 main(float3 position_ : POSITION, uint instanceId : SV_InstanceID) : SV_POSITION
{
    const ScatterBatchGPUData batch = ScatterBatchBuffer[g_draw.instanceIndex_];
    const float3 posRel = ScatterPosRel(batch, ScatterInstance(batch, instanceId), position_);
    return mul(ShadowBuffer[g_draw.shadowViewIndex_].viewProj_, float4(posRel, 1.0f));
}
