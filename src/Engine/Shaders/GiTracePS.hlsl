#include "GiPath.hlsli"

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData fc = FrameBuffer[0];
    const uint2 pixel = uint2(svpos.xy);
    const uint thread = pixel.y * RtCacheTraceWidth + pixel.x;
    const uint cells = min(GiWork[RtCacheWorkCount], RtCacheListCapacity);
    if (cells == 0u)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    const uint budget = min(fc.giWidth_ * fc.giHeight_, RtCacheListCapacity);
    const uint rays = max(budget / cells, 1u);
    const uint cell = thread / rays;
    if (cell >= cells)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    const uint slot = GiWork[RtCacheWorkList + cell];
    const uint candidate = GiValues[slot].vote_ & (RtCacheCandidateCapacity - 1u);
    const uint at = RtCacheWorkCandidates + candidate * RtCacheCandidateWords;
    RtHit from = (RtHit)0;
    from.position_ = asfloat(uint3(GiWork[at + 0u], GiWork[at + 1u], GiWork[at + 2u]));
    from.normal_ = asfloat(uint3(GiWork[at + 3u], GiWork[at + 4u], GiWork[at + 5u]));

    const uint ray = thread - cell * rays;
    if (g_draw.instanceIndex_ == 0u && ray == 0u)
        GiValidateProbe(slot);

    const uint sampleIndex = fc.frameIndex_ * fc.giPasses_ + g_draw.instanceIndex_;
    uint seed = GiCellRaySeed(slot, ray, sampleIndex);
    const uint bucket = slot & (RtCacheStatsBuckets - 1u);
    GiReserve(slot);
    const float2 firstU = float2(GiRand(seed), GiRand(seed));
    const uint probe = slot * RtProbeWords;
    const bool unprobed = ray < 512u && GiProbes[probe + RtProbeState] == RtProbeNone;
    float firstDirect;
    uint firstSlot;
    const float3 irradiance = GiPathIrradiance(from, firstU, seed, bucket,
                                               GiProbes[probe + RtProbeEvent], unprobed,
                                               firstDirect, firstSlot);
    GiAccumulate(slot, irradiance, fc.exposure_);
    if (ray >= 512u)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    const uint pick = (g_draw.instanceIndex_ << 9) | ray;
    if (firstDirect > 0.0f)
        InterlockedMax(GiProbes[probe + RtProbeBest],
                       GiBestLit | (asuint(firstDirect) & GiBestLuminance) | pick);
    else if (firstSlot != GiNoSlot &&
             asfloat(GiProbes[firstSlot * RtProbeWords + RtProbeRadiance]) > 0.0f)
        InterlockedMax(GiProbes[probe + RtProbeBest],
                       (asuint(GiLuminance(irradiance)) & GiBestLuminance) | pick);
    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}
