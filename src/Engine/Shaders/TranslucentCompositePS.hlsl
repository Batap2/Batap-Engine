#include "ShaderInterop.h"
#include "Translucency.hlsli"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::binding(FrameConstantsBinding, FrameSet)]] StructuredBuffer<FrameGPUData> FrameBuffer;

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData frame = FrameBuffer[0];
    const int3 texel = int3(svpos.xy, 0);
    const float reveal = g_textures[frame.translucentRevealTexture_].Load(texel).r;
    if (reveal >= 1.0f)
        discard;
    const float4 accum = g_textures[frame.translucentAccumTexture_].Load(texel);
    const float3 average = accum.rgb / max(accum.a, 1e-12f);
    const float coverage = 1.0f - reveal;
    return float4(min(average * coverage, TranslucentRadianceMax), coverage);
}
