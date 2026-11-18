#include "ReflectTrace.hlsli"

[numthreads(ScreenGroupSize, ScreenGroupSize, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 size = ScreenImageSize(g_draw.storageImage_);
    if (any(id.xy >= size))
        return;
    const FrameGPUData frame = FrameBuffer[0];
    const int2 pixel = int2(id.xy);
    if (all(ReflectTracedPixel(pixel >> 1, 1u, ReflectTraceOffset(frame.frameIndex_), int2(size)) ==
            pixel))
        return;
    const ReflectSurface surface =
        ReflectSurfaceDecode(g_uintTextures[frame.reflectSurfaceTexture_].Load(int3(pixel, 0)).xy);
    if (surface.kind_ == ReflectKindNone || surface.roughness_ > ReflectOwnRayRoughness)
        return;
    ReflectTraceInto(id.xy, size, id.xy);
}
