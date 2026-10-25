#include "ShaderInterop.h"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::binding(FrameConstantsBinding, FrameSet)]] StructuredBuffer<FrameGPUData> FrameBuffer;

static const uint ExposureSamples = 4u;
static const float ExposureFloor = 1e-3f;

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData frame = FrameBuffer[0];
    uint width;
    uint height;
    g_textures[frame.hdrTexture_].GetDimensions(width, height);
    const float2 cell = float2(width, height) / float(ExposureGridSize);
    const float2 origin = floor(svpos.xy) * cell;

    float sumLog = 0.0f;
    float count = 0.0f;
    for (uint j = 0; j < ExposureSamples; ++j)
    {
        for (uint i = 0; i < ExposureSamples; ++i)
        {
            const float2 at = origin + (float2(i, j) + 0.5f) * (cell / float(ExposureSamples));
            const float3 c = g_textures[frame.hdrTexture_].Load(int3(at, 0)).rgb;
            const float luminance = dot(c, float3(0.2126f, 0.7152f, 0.0722f)) / frame.exposure_;
            if (luminance > ExposureFloor)
            {
                sumLog += log2(luminance);
                count += 1.0f;
            }
        }
    }
    return float4(sumLog, count, 0.0f, 0.0f);
}
