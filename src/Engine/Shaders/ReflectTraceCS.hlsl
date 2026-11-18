#include "ReflectTrace.hlsli"

[numthreads(ScreenGroupSize, ScreenGroupSize, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= ScreenImageSize(g_draw.storageImage_)))
        return;
    const FrameGPUData frame = FrameBuffer[0];
    uint2 size;
    g_textures[frame.depthTexture_].GetDimensions(size.x, size.y);
    const int2 pixel = ReflectTracedPixel(int2(id.xy), frame.reflectTraceShift_,
                                          ReflectTraceOffset(frame.frameIndex_), int2(size));
    ReflectTraceInto(uint2(pixel), size, id.xy);
}
