#include "ScreenCompute.hlsli"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::binding(FrameConstantsBinding, FrameSet)]] StructuredBuffer<FrameGPUData> FrameBuffer;
[[vk::push_constant]] DrawPush g_draw;

static const uint ExposureSamples = 4u;
static const float ExposureFloor = 1e-3f;

float4 ExposureMeasure(float4 svpos)
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

[numthreads(ScreenGroupSize, ScreenGroupSize, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 size = ScreenImageSize(g_draw.storageImage_);
    if (any(id.xy >= size))
        return;
    const float4 svpos = float4(float2(id.xy) + 0.5f, 0.0f, 1.0f);
    g_storageImages[g_draw.storageImage_][id.xy] = ExposureMeasure(svpos);
}
