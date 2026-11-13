#include "ScatterCull.hlsli"

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

[numthreads(ScatterCullThreads, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint thread : SV_GroupIndex)
{
    const uint batchIndex = ScatterBatchIndex(groupId);
    if (batchIndex >= g_draw.instanceIndex_)
        return;
    const ScatterBatchGPUData batch = ScatterBatchBuffer[batchIndex];
    if (batch.instanceCount_ == 0u)
        return;
    const ScatterCullGPUData cull = ScatterCullBuffer[0];

    if (thread == 0u)
    {
        uint regions = 0u;
        const float4 bounds = ScatterBounds(batch);
        const bool inView =
            ScatterInView(bounds.xyz, bounds.w, CameraInstancebuffer[g_draw.cameraIndex_]);
        g_inView = inView ? 1u : 0u;
        if (inView && cull.hiz_ == 0u)
            ScatterReserve(batch.drawGroup_, batch.drawGroups_, 0u, batch.instanceCount_, cull,
                           regions);
        if (batch.shadowGroups_ != 0u)
        {
            for (uint c = 0u; c < cull.cascadeCount_; ++c)
                if (ScatterCastsShadow(bounds, batch.shadowLength_, c, cull))
                    ScatterReserve(batch.shadowGroup_, batch.shadowGroups_, 1u + c,
                                   batch.instanceCount_, cull, regions);
            if (cull.localViews_ != 0u)
                ScatterReserve(batch.shadowGroup_, batch.shadowGroups_, ScatterCullViews - 1u,
                               batch.instanceCount_, cull, regions);
        }
        g_regionCount = regions;
    }
    ScatterWriteRegions(batch, thread);
    if (cull.hiz_ == 0u || g_inView == 0u)
        return;
    for (uint base = 0u; base < batch.instanceCount_; base += ScatterCullThreads)
    {
        const uint instance = batch.firstInstance_ + base + thread;
        bool drawn = false;
        if (base + thread < batch.instanceCount_)
            drawn = ScatterVisibilityBuffer[instance] != 0u;
        ScatterEmitChunk(batch, 0u, thread, instance, drawn, cull);
    }
}
