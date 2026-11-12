#ifndef BATAP_SCATTER_HLSLI
#define BATAP_SCATTER_HLSLI

#include "ShaderInterop.h"

[[vk::binding(ScatterBatchesBinding, FrameSet)]]
StructuredBuffer<ScatterBatchGPUData> ScatterBatchBuffer;
[[vk::binding(ScatterInstancesBinding, FrameSet)]]
StructuredBuffer<ScatterInstanceGPUData> ScatterInstanceBuffer;
[[vk::binding(ScatterOwnersBinding, FrameSet)]]
StructuredBuffer<ScatterOwnerGPUData> ScatterOwnerBuffer;

float3 ScatterRotate(float4 q, float3 v)
{
    const float3 t = 2.0f * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

ScatterInstanceGPUData ScatterInstance(ScatterBatchGPUData batch, uint instanceId)
{
    return ScatterInstanceBuffer[batch.firstInstance_ + instanceId];
}

float4x4 ScatterPlacedMatrix(float4x4 shape, float4x4 axes, int3 cells, float3 offset)
{
    const float3 t = mul((float3x3)axes, float3(cells) * GiCellBase + offset);
    float4x4 m = shape;
    m[0][3] = t.x;
    m[1][3] = t.y;
    m[2][3] = t.z;
    return m;
}

TerrainWorldGPUData ScatterPlaced(ScatterBatchGPUData batch)
{
    const ScatterOwnerGPUData owner = ScatterOwnerBuffer[batch.ownerSlot_];
    TerrainWorldGPUData placed;
    placed.world_ = ScatterPlacedMatrix(owner.linear_, owner.axes_,
                                        batch.centreCell_ - owner.cameraCell_,
                                        batch.centreOffset_ - owner.cameraOffset_);
    placed.prevWorld_ = ScatterPlacedMatrix(owner.prevLinear_, owner.prevAxes_,
                                            batch.centreCell_ - owner.prevCameraCell_,
                                            batch.centreOffset_ - owner.prevCameraOffset_);
    placed.centreCell_ = batch.centreCell_;
    placed.centreOffset_ = batch.centreOffset_;
    placed.owner_ = batch.owner_;
    placed.tracedBase_ = TerrainNoTraced;
    return placed;
}

float3 ScatterPosRel(ScatterBatchGPUData batch, TerrainWorldGPUData placed,
                     ScatterInstanceGPUData inst, float3 position)
{
    const float3 origin = placed.world_._m03_m13_m23 + mul((float3x3)placed.world_, inst.position_);
    float scale = inst.scale_;
    if (batch.fadeEnd_ > 0.0f)
        scale *= saturate((batch.fadeEnd_ - length(origin)) /
                          max(batch.fadeEnd_ - batch.fadeStart_, 1e-3f));
    return origin + mul((float3x3)placed.world_, ScatterRotate(inst.rotation_, position * scale));
}

float3 ScatterDirection(TerrainWorldGPUData placed, ScatterInstanceGPUData inst, float3 direction)
{
    return normalize(mul((float3x3)placed.world_, ScatterRotate(inst.rotation_, direction)));
}

#endif
