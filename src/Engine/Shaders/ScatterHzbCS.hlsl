#include "Scatter.hlsli"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::binding(ScatterHzbBinding, FrameSet)]]
RWStructuredBuffer<float> ScatterHzbOut;
[[vk::binding(ScatterCullBinding, FrameSet)]]
StructuredBuffer<ScatterCullGPUData> ScatterCullBuffer;

[[vk::push_constant]] DrawPush g_draw;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const ScatterCullGPUData cull = ScatterCullBuffer[0];
    const uint2 screen = uint2(cull.screenWidth_, cull.screenHeight_);
    const uint level = g_draw.instanceIndex_;
    const uint2 size = ScatterHzbSize(level, screen);
    if (any(id.xy >= size))
        return;
    float farthest = 1e30f;
    if (level == 0u)
    {
        for (uint y = 0u; y < 2u; ++y)
            for (uint x = 0u; x < 2u; ++x)
            {
                const uint2 p = min(id.xy * 2u + uint2(x, y), screen - 1u);
                farthest = min(farthest, g_textures[g_draw.submeshIndex_].Load(int3(p, 0)).r);
            }
    }
    else
    {
        const uint2 below = ScatterHzbSize(level - 1u, screen);
        const uint base = ScatterHzbOffset(level - 1u, screen);
        for (uint y = 0u; y < 2u; ++y)
            for (uint x = 0u; x < 2u; ++x)
            {
                const uint2 p = min(id.xy * 2u + uint2(x, y), below - 1u);
                farthest = min(farthest, ScatterHzbOut[base + p.y * below.x + p.x]);
            }
    }
    ScatterHzbOut[ScatterHzbOffset(level, screen) + id.y * size.x + id.x] = farthest;
}
