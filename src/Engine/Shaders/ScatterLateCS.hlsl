#include "ScatterCull.hlsli"

[[vk::binding(ScatterHzbBinding, FrameSet)]]
StructuredBuffer<float> ScatterHzbBuffer;

// Reversed depth: a box whose nearest corner lies beyond the farthest depth
// of the pyramid texels it covers is hidden.
bool ScatterOccluded(float4 bounds, CameraGPUData cam, ScatterCullGPUData cull)
{
    if (CameraOrtho(cam) || cull.hzbLevels_ == 0u)
        return false;
    float2 lo = 1e30f;
    float2 hi = -1e30f;
    float nearest = 0.0f;
    [unroll] for (uint i = 0u; i < 8u; ++i)
    {
        const float3 corner =
            bounds.xyz + bounds.w * float3((i & 1u) != 0u ? 1.0f : -1.0f,
                                           (i & 2u) != 0u ? 1.0f : -1.0f,
                                           (i & 4u) != 0u ? 1.0f : -1.0f);
        const float4 clip = mul(cam.proj_, float4(mul((float3x3)cam.view_, corner), 1.0f));
        if (clip.w <= 1e-5f)
            return false;
        const float3 ndc = clip.xyz / clip.w;
        lo = min(lo, ndc.xy);
        hi = max(hi, ndc.xy);
        nearest = max(nearest, ndc.z);
    }
    const uint2 screen = uint2(cull.screenWidth_, cull.screenHeight_);
    const float2 size = float2(screen);
    const float2 pmin = float2(lo.x * 0.5f + 0.5f, 0.5f - 0.5f * hi.y) * size;
    const float2 pmax = float2(hi.x * 0.5f + 0.5f, 0.5f - 0.5f * lo.y) * size;
    if (pmax.x < 0.0f || pmax.y < 0.0f || pmin.x >= size.x || pmin.y >= size.y)
        return true;
    const uint2 a = uint2(clamp(pmin, 0.0f, size - 1.0f));
    const uint2 b = uint2(clamp(pmax, 0.0f, size - 1.0f));
    const uint span = max(b.x - a.x, b.y - a.y);
    const uint level = min(span == 0u ? 0u : firstbithigh(span), cull.hzbLevels_ - 1u);
    const uint2 texels = ScatterHzbSize(level, screen);
    const uint offset = ScatterHzbOffset(level, screen);
    const uint2 ta = min(a >> (level + 1u), texels - 1u);
    const uint2 tb = min(b >> (level + 1u), texels - 1u);
    float farthest = 1e30f;
    for (uint y = ta.y; y <= tb.y; ++y)
        for (uint x = ta.x; x <= tb.x; ++x)
            farthest = min(farthest, ScatterHzbBuffer[offset + y * texels.x + x]);
    return nearest < farthest;
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
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    if (thread == 0u)
    {
        const float4 bounds = ScatterBounds(batch);
        g_inView = 0u;
        if (ScatterInView(bounds.xyz, bounds.w, cam))
            g_inView = ScatterOccluded(bounds, cam, cull) ? 1u : 2u;
    }
    GroupMemoryBarrierWithGroupSync();
    const uint state = g_inView;
    if (state == 0u)
        return;
    if (state == 1u)
    {
        for (uint i = thread; i < batch.instanceCount_; i += ScatterCullThreads)
            ScatterVisibilityBuffer[batch.firstInstance_ + i] = 0u;
        return;
    }
    const float4x4 world = ScatterPlaced(batch).world_;
    const float ownerScale = ScatterOwnerScale(batch);
    for (uint base = 0u; base < batch.instanceCount_; base += ScatterCullThreads)
    {
        const uint instance = batch.firstInstance_ + base + thread;
        bool late = false;
        if (base + thread < batch.instanceCount_)
        {
            const float4 bounds = ScatterInstanceBounds(batch, world, ownerScale, instance);
            const bool visible =
                ScatterInView(bounds.xyz, bounds.w, cam) && !ScatterOccluded(bounds, cam, cull);
            late = visible && ScatterVisibilityBuffer[instance] == 0u;
            ScatterVisibilityBuffer[instance] = visible ? 1u : 0u;
        }
        ScatterEmitChunk(batch, ScatterLateView, thread, instance, late, cull);
    }
}
