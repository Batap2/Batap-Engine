#ifndef BATAP_SCATTER_CULL_HLSLI
#define BATAP_SCATTER_CULL_HLSLI

#define BATAP_SCATTER_CULL
#include "Camera.hlsli"
#include "Scatter.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::binding(ScatterDrawGroupsBinding, FrameSet)]]
StructuredBuffer<ScatterDrawGroupGPUData> ScatterDrawGroupBuffer;
[[vk::binding(ScatterDrawCommandsBinding, FrameSet)]]
RWByteAddressBuffer ScatterDrawCommandWords;
[[vk::binding(ScatterVisibleInstancesBinding, FrameSet)]]
RWStructuredBuffer<uint> ScatterVisibleOut;
[[vk::binding(ScatterCullBinding, FrameSet)]]
StructuredBuffer<ScatterCullGPUData> ScatterCullBuffer;
[[vk::binding(ScatterVisibilityBinding, FrameSet)]]
RWStructuredBuffer<uint> ScatterVisibilityBuffer;

[[vk::push_constant]] DrawPush g_draw;

static const uint ScatterCullThreads = 64u;
static const uint ScatterMaxRegions = 64u;
static const uint ScatterMaxSubmeshes = 8u;

groupshared uint g_regionStarts[ScatterMaxRegions];
groupshared uint g_regionCount;
groupshared uint g_scan[ScatterCullThreads];
groupshared uint g_groupBases[ScatterMaxSubmeshes];
groupshared uint g_inView;

bool ScatterInCone(float3 centre, float radius, float3 forward, float cone)
{
    const float distance = length(centre);
    if (distance <= radius)
        return true;
    const float angle = acos(clamp(dot(centre, forward) / distance, -1.0f, 1.0f));
    return angle - asin(radius / distance) <= cone;
}

bool ScatterInView(float3 centre, float radius, CameraGPUData cam)
{
    if (CameraOrtho(cam) || radius <= 0.0f)
        return true;
    const float tanX = 1.0f / cam.proj_[0][0];
    const float tanY = 1.0f / cam.proj_[1][1];
    return ScatterInCone(centre, radius, cam.fwd_, atan(sqrt(tanX * tanX + tanY * tanY)));
}

uint ScatterBatchIndex(uint3 groupId)
{
    return groupId.y * ScatterCullRow + groupId.x;
}

float ScatterOwnerScale(ScatterBatchGPUData batch)
{
    const float3x3 shape = (float3x3)ScatterOwnerBuffer[batch.ownerSlot_].linear_;
    return max(length(float3(shape[0][0], shape[1][0], shape[2][0])),
               max(length(float3(shape[0][1], shape[1][1], shape[2][1])),
                   length(float3(shape[0][2], shape[1][2], shape[2][2]))));
}

float4 ScatterBounds(ScatterBatchGPUData batch)
{
    return float4(ScatterPlaced(batch).world_._m03_m13_m23,
                  (batch.radius_ + batch.impostorRadius_) * ScatterOwnerScale(batch));
}

float4 ScatterInstanceBounds(ScatterBatchGPUData batch, float4x4 world, float ownerScale,
                             uint instance)
{
    const ScatterInstanceGPUData inst = ScatterInstanceBuffer[instance];
    return float4(world._m03_m13_m23 + mul((float3x3)world, inst.position_),
                  batch.pieceRadius_ * inst.scale_ * ownerScale);
}

uint ScatterRegionStart(ScatterDrawGroupGPUData g, uint view, ScatterCullGPUData cull)
{
    if (view == 0u)
        return g.offset_;
    if (view == ScatterLateView)
        return g.offset_ + cull.mainStride_;
    return g.shadowOffset_ + (view - 1u) * cull.shadowStride_;
}

void ScatterReserve(uint first, uint count, uint view, uint instances, ScatterCullGPUData cull,
                    inout uint regions)
{
    const bool main = view == 0u || view == ScatterLateView;
    for (uint s = 0u; s < count && regions < ScatterMaxRegions; ++s)
    {
        const uint group = first + s;
        const ScatterDrawGroupGPUData g = ScatterDrawGroupBuffer[group];
        if ((main ? g.capacity_ : g.shadowCapacity_) == 0u)
            continue;
        uint base;
        ScatterDrawCommandWords.InterlockedAdd((view * ScatterMaxDrawGroups + group) * 20u + 4u,
                                               instances, base);
        g_regionStarts[regions] = ScatterRegionStart(g, view, cull) + base;
        ++regions;
    }
}

void ScatterWriteRegions(ScatterBatchGPUData batch, uint thread)
{
    GroupMemoryBarrierWithGroupSync();
    const uint regionCount = g_regionCount;
    for (uint r = 0u; r < regionCount; ++r)
    {
        const uint start = g_regionStarts[r];
        for (uint i = thread; i < batch.instanceCount_; i += ScatterCullThreads)
            ScatterVisibleOut[start + i] = batch.firstInstance_ + i;
    }
}

void ScatterEmitChunk(ScatterBatchGPUData batch, uint view, uint thread, uint instance, bool flag,
                      ScatterCullGPUData cull)
{
    g_scan[thread] = flag ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();
    for (uint step = 1u; step < ScatterCullThreads; step <<= 1u)
    {
        uint sum = g_scan[thread];
        if (thread >= step)
            sum += g_scan[thread - step];
        GroupMemoryBarrierWithGroupSync();
        g_scan[thread] = sum;
        GroupMemoryBarrierWithGroupSync();
    }
    const uint total = g_scan[ScatterCullThreads - 1u];
    const uint before = g_scan[thread] - (flag ? 1u : 0u);
    if (thread == 0u && total != 0u)
        for (uint s = 0u; s < batch.drawGroups_ && s < ScatterMaxSubmeshes; ++s)
        {
            const uint group = batch.drawGroup_ + s;
            const ScatterDrawGroupGPUData g = ScatterDrawGroupBuffer[group];
            g_groupBases[s] = InvalidGPUIndex;
            if (g.capacity_ == 0u)
                continue;
            uint base;
            ScatterDrawCommandWords.InterlockedAdd(
                (view * ScatterMaxDrawGroups + group) * 20u + 4u, total, base);
            g_groupBases[s] = ScatterRegionStart(g, view, cull) + base;
        }
    GroupMemoryBarrierWithGroupSync();
    if (flag)
        for (uint k = 0u; k < batch.drawGroups_ && k < ScatterMaxSubmeshes; ++k)
            if (g_groupBases[k] != InvalidGPUIndex)
                ScatterVisibleOut[g_groupBases[k] + before] = instance;
    GroupMemoryBarrierWithGroupSync();
}

#endif
