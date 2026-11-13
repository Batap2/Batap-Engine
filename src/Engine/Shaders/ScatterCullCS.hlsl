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

[[vk::push_constant]] DrawPush g_draw;

static const uint ScatterCullThreads = 64u;
static const uint ScatterMaxRegions = 64u;

groupshared uint g_regionStarts[ScatterMaxRegions];
groupshared uint g_regionCount;

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

// The CPU path keeps the same tests in ScatterDrawer.cpp (castsInto,
// shadowStaysIn, shadowInView): change both together.
bool ScatterCastsInto(float4 bounds, float4 cascade, float3 lightDir)
{
    const float3 offset = bounds.xyz - cascade.xyz;
    const float along = dot(offset, lightDir);
    const float reach = bounds.w + cascade.w;
    const float3 side = offset - along * lightDir;
    return along <= reach && dot(side, side) <= reach * reach;
}

bool ScatterShadowStaysIn(float4 bounds, float shadowLength, float3 lightDir, uint finer,
                          ScatterCullGPUData cull)
{
    const float3 far = bounds.xyz + lightDir * shadowLength;
    for (uint c = 0u; c < finer; ++c)
    {
        const float4 sphere = cull.cascades_[c];
        const float inside = sphere.w * (1.0f - CascadeFadeBand) - bounds.w;
        const float3 toNear = bounds.xyz - sphere.xyz;
        const float3 toFar = far - sphere.xyz;
        if (inside > 0.0f && dot(toNear, toNear) <= inside * inside &&
            dot(toFar, toFar) <= inside * inside)
            return true;
    }
    return false;
}

bool ScatterCastsShadow(float4 bounds, float shadowLength, uint cascade, ScatterCullGPUData cull)
{
    const float3 lightDir = cull.lightDirs_[cascade].xyz;
    if (!ScatterCastsInto(bounds, cull.cascades_[cascade], lightDir))
        return false;
    if (shadowLength <= 0.0f)
        return true;
    if (cull.viewCone_ > 0.0f &&
        !ScatterInCone(bounds.xyz + lightDir * (0.5f * shadowLength),
                       bounds.w + 0.5f * shadowLength, cull.viewForward_, cull.viewCone_))
        return false;
    return !ScatterShadowStaysIn(bounds, shadowLength, lightDir, cascade, cull);
}

void ScatterReserve(uint first, uint count, uint view, uint instances, uint shadowStride,
                    inout uint regions)
{
    for (uint s = 0u; s < count && regions < ScatterMaxRegions; ++s)
    {
        const uint group = first + s;
        const ScatterDrawGroupGPUData g = ScatterDrawGroupBuffer[group];
        const uint capacity = view == 0u ? g.capacity_ : g.shadowCapacity_;
        if (capacity == 0u)
            continue;
        const uint command = view * ScatterMaxDrawGroups + group;
        uint base;
        ScatterDrawCommandWords.InterlockedAdd(command * 20u + 4u, instances, base);
        const uint start = view == 0u ? g.offset_ : g.shadowOffset_ + (view - 1u) * shadowStride;
        g_regionStarts[regions] = start + base;
        ++regions;
    }
}

[numthreads(ScatterCullThreads, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint thread : SV_GroupIndex)
{
    const uint batchIndex = groupId.y * ScatterCullRow + groupId.x;
    if (batchIndex >= g_draw.instanceIndex_)
        return;
    const ScatterBatchGPUData batch = ScatterBatchBuffer[batchIndex];
    if (batch.instanceCount_ == 0u)
        return;

    if (thread == 0u)
    {
        uint regions = 0u;
        const float3x3 shape = (float3x3)ScatterOwnerBuffer[batch.ownerSlot_].linear_;
        const float scale = max(length(float3(shape[0][0], shape[1][0], shape[2][0])),
                                max(length(float3(shape[0][1], shape[1][1], shape[2][1])),
                                    length(float3(shape[0][2], shape[1][2], shape[2][2]))));
        const float4 bounds =
            float4(ScatterPlaced(batch).world_._m03_m13_m23, batch.radius_ * scale);
        if (ScatterInView(bounds.xyz, bounds.w, CameraInstancebuffer[g_draw.cameraIndex_]))
            ScatterReserve(batch.drawGroup_, batch.drawGroups_, 0u, batch.instanceCount_, 0u,
                           regions);
        if (batch.shadowGroups_ != 0u)
        {
            const ScatterCullGPUData cull = ScatterCullBuffer[0];
            for (uint c = 0u; c < cull.cascadeCount_; ++c)
                if (ScatterCastsShadow(bounds, batch.shadowLength_, c, cull))
                    ScatterReserve(batch.shadowGroup_, batch.shadowGroups_, 1u + c,
                                   batch.instanceCount_, cull.shadowStride_, regions);
            if (cull.localViews_ != 0u)
                ScatterReserve(batch.shadowGroup_, batch.shadowGroups_, ScatterCullViews - 1u,
                               batch.instanceCount_, cull.shadowStride_, regions);
        }
        g_regionCount = regions;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint regionCount = g_regionCount;
    for (uint r = 0u; r < regionCount; ++r)
    {
        const uint start = g_regionStarts[r];
        for (uint i = thread; i < batch.instanceCount_; i += ScatterCullThreads)
            ScatterVisibleOut[start + i] = batch.firstInstance_ + i;
    }
}
