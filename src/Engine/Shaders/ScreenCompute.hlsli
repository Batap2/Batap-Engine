#ifndef BATAP_SCREEN_COMPUTE_HLSLI
#define BATAP_SCREEN_COMPUTE_HLSLI

#include "ShaderInterop.h"

[[vk::binding(StorageImagesBinding, BindlessSet)]] [[vk::image_format("unknown")]]
RWTexture2D<float4> g_storageImages[];
[[vk::binding(StorageImagesBinding, BindlessSet)]] [[vk::image_format("unknown")]]
RWTexture2D<uint4> g_storageUintImages[];

uint2 ScreenImageSize(uint image)
{
    uint width;
    uint height;
    g_storageImages[image].GetDimensions(width, height);
    return uint2(width, height);
}

// What SkyVS interpolates at a pixel centre: y up, unlike the pixel rows.
float2 ScreenNdc(uint2 pixel, uint2 size)
{
    const float2 uv = (float2(pixel) + 0.5f) / float2(size);
    return float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
}

#endif
