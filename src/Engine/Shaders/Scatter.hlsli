#ifndef BATAP_SCATTER_HLSLI
#define BATAP_SCATTER_HLSLI

#include "ShaderInterop.h"

[[vk::binding(ScatterBatchesBinding, FrameSet)]]
StructuredBuffer<ScatterBatchGPUData> ScatterBatchBuffer;
[[vk::binding(ScatterInstancesBinding, FrameSet)]]
StructuredBuffer<ScatterInstanceGPUData> ScatterInstanceBuffer;

float3 ScatterRotate(float4 q, float3 v)
{
    const float3 t = 2.0f * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

ScatterInstanceGPUData ScatterInstance(ScatterBatchGPUData batch, uint instanceId)
{
    return ScatterInstanceBuffer[batch.firstInstance_ + instanceId];
}

float3 ScatterPosRel(ScatterBatchGPUData batch, ScatterInstanceGPUData inst, float3 position)
{
    const float3 origin =
        batch.placed_.world_._m03_m13_m23 + mul((float3x3)batch.placed_.world_, inst.position_);
    float scale = inst.scale_;
    if (batch.fadeEnd_ > 0.0f)
        scale *= saturate((batch.fadeEnd_ - length(origin)) /
                          max(batch.fadeEnd_ - batch.fadeStart_, 1e-3f));
    return origin + mul((float3x3)batch.placed_.world_, ScatterRotate(inst.rotation_, position * scale));
}

float3 ScatterDirection(ScatterBatchGPUData batch, ScatterInstanceGPUData inst, float3 direction)
{
    return normalize(mul((float3x3)batch.placed_.world_, ScatterRotate(inst.rotation_, direction)));
}

#endif
