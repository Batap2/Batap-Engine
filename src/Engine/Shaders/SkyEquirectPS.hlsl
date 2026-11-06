#include "Lighting.hlsli"

float4 main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0) : SV_Target
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
