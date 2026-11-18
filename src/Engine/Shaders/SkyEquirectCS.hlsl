#include "Lighting.hlsli"
#include "ScreenCompute.hlsli"

float4 SkyEquirect(float4 svpos, float2 ndc)
{
    float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    SkyboxGPUData sky = SkyboxBuffer[0];
    float mip = 0.0f;
    if (sky.mode == 0u && sky.bindlessIndex != InvalidGPUIndex)
    {
        uint w, h;
        g_textures[sky.bindlessIndex].GetDimensions(w, h);
        mip = max(log2(float(w) / float(SkyPrefilterWidth)), 0.0f);
    }
    return float4(SampleSky(SkyEquirectDir(uv), mip), 1.0f);
}

[numthreads(ScreenGroupSize, ScreenGroupSize, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 size = ScreenImageSize(g_draw.storageImage_);
    if (any(id.xy >= size))
        return;
    const float4 svpos = float4(float2(id.xy) + 0.5f, 0.0f, 1.0f);
    g_storageImages[g_draw.storageImage_][id.xy] = SkyEquirect(svpos, ScreenNdc(id.xy, size));
}
