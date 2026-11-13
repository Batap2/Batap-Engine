#include "Scatter.hlsli"
#include "Shadows.hlsli"

[[vk::push_constant]] DrawPush g_draw;

float4 main(float3 position_ : POSITION, uint instanceId : SV_InstanceID) : SV_POSITION
{
    const uint instance = ScatterInstanceOf(instanceId, g_draw.instanceIndex_);
    const ScatterBatchGPUData batch = ScatterBatchBuffer[ScatterBatchOf(instance)];
    const float3 posRel =
        ScatterPosRel(batch, ScatterPlaced(batch), ScatterInstanceBuffer[instance], position_);
    return mul(ShadowBuffer[g_draw.shadowViewIndex_].viewProj_, float4(posRel, 1.0f));
}
