#include "ShaderInterop.h"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::push_constant]] DrawPush g_draw;

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    const int2 base = int2(floor(svpos.xy)) * int(ExposureReduceBlock);
    float2 sum = float2(0.0f, 0.0f);
    for (int j = 0; j < int(ExposureReduceBlock); ++j)
        for (int i = 0; i < int(ExposureReduceBlock); ++i)
            sum += g_textures[g_draw.instanceIndex_].Load(int3(base + int2(i, j), 0)).xy;
    return float4(sum, 0.0f, 0.0f);
}
