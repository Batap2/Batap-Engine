#include "GiPath.hlsli"
#include "ScreenCompute.hlsli"

[numthreads(ScreenGroupSize, ScreenGroupSize, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const FrameGPUData fc = FrameBuffer[0];
    const uint2 pixel = id.xy;
    const uint2 size = uint2(fc.giWidth_, fc.giHeight_);
    if (any(pixel >= size))
        return;
    const float2 ndc = ScreenNdc(pixel, size);
    const uint sampleIndex = fc.frameIndex_ * fc.giPasses_ + g_draw.instanceIndex_;
    uint seed = GiHash(pixel.x + GiHash(pixel.y + GiHash(sampleIndex)));

    const uint2 pixelRotation = uint2(GiHash(pixel.x * 7919u + pixel.y), GiHash(pixel.y * 104729u + pixel.x + 1u));
    const float2 jitterU = GiStratified ? GiR2(sampleIndex, pixelRotation)
                                        : float2(GiRand(seed), GiRand(seed));
    const float2 jitter = (jitterU - 0.5f) *
                          (2.0f / float2(max(fc.giWidth_, 1u), max(fc.giHeight_, 1u)));

    RtHit first;
    if (!GiCameraHit(ndc + jitter, first))
        return;

    const uint2 key = GiCacheKey(first.gi_, GiRandomLevel(length(first.position_), GiRand(seed)));
    const uint bucket = (pixel.x ^ pixel.y) & (RtCacheStatsBuckets - 1u);
    bool inserted;
    const uint slot = GiSlotInsert(key, inserted);
    if (slot == GiNoSlot)
    {
        GiStatAdd(RtCacheStatFailed, bucket);
        return;
    }
    if (inserted)
        GiStatAdd(RtCacheStatInserted, bucket);

    const uint place = GiReserve(slot);
    const uint folded = GiValues[slot].history_ & GiSamplesMask;
    uint2 cellRotation = uint2(GiHash(key.x), GiHash(key.y ^ 0x9E3779B9u));
    uint cellIndex = folded + place;
    if (folded >= GiSamplesMask)
    {
        cellRotation = uint2(GiHash(cellRotation.x ^ fc.frameIndex_), GiHash(cellRotation.y + fc.frameIndex_));
        cellIndex = place;
    }
    const float2 firstU = GiStratified ? GiR2(cellIndex, cellRotation)
                                       : float2(GiRand(seed), GiRand(seed));

    float firstDirect;
    uint firstSlot;
    const float3 irradiance =
        GiPathIrradiance(first, firstU, seed, bucket, 0u, false, firstDirect, firstSlot);
    GiAccumulate(slot, irradiance, fc.exposure_);
}
