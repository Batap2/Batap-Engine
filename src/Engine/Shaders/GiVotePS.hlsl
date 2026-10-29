#include "GiPath.hlsli"

float4 main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0) : SV_Target
{
    const FrameGPUData fc = FrameBuffer[0];
    const uint2 pixel = uint2(svpos.xy);
    uint seed = GiHash(pixel.x + GiHash(pixel.y + GiHash(fc.frameIndex_)));
    const float2 jitter = (float2(GiRand(seed), GiRand(seed)) - 0.5f) *
                          (2.0f / float2(max(fc.giWidth_, 1u), max(fc.giHeight_, 1u)));

    RtHit first;
    if (!GiCameraHit(ndc + jitter, first))
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    const uint2 key = GiCacheKey(first.gi_, GiRandomLevel(length(first.position_), GiRand(seed)));
    const uint bucket = (pixel.x ^ pixel.y) & (RtCacheStatsBuckets - 1u);
    bool inserted;
    const uint slot = GiSlotInsert(key, inserted);
    if (slot == GiNoSlot)
    {
        GiStatAdd(RtCacheStatFailed, bucket);
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    if (inserted)
        GiStatAdd(RtCacheStatInserted, bucket);

    const uint candidate = pixel.y * fc.giWidth_ + pixel.x;
    if (candidate >= RtCacheCandidateCapacity)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    const uint at = RtCacheWorkCandidates + candidate * RtCacheCandidateWords;
    GiWork[at + 0u] = asuint(first.position_.x);
    GiWork[at + 1u] = asuint(first.position_.y);
    GiWork[at + 2u] = asuint(first.position_.z);
    GiWork[at + 3u] = asuint(first.normal_.x);
    GiWork[at + 4u] = asuint(first.normal_.y);
    GiWork[at + 5u] = asuint(first.normal_.z);

    uint previous;
    InterlockedMax(GiValues[slot].vote_, ((GiHash(seed) | 1u) << RtCacheCandidateBits) | candidate,
                   previous);
    if (previous == 0u)
    {
        uint index;
        InterlockedAdd(GiWork[RtCacheWorkCount], 1u, index);
        if (index < RtCacheListCapacity)
            GiWork[RtCacheWorkList + index] = slot;
    }
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}
