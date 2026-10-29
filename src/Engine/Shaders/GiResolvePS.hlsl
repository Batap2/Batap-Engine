#include "GiPath.hlsli"

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData fc = FrameBuffer[0];
    const uint2 pixel = uint2(svpos.xy);
    const uint entry = pixel.y * RtCacheResolveWidth + pixel.x;
    const uint bucket = entry & (RtCacheStatsBuckets - 1u);
    const float4 none = float4(0.0f, 0.0f, 0.0f, 0.0f);

    const uint64_t key = GiKeys[entry];
    if (key == 0)
        return none;

    RtCacheValueGPUData v = GiValues[entry];
    uint samples = v.history_ & GiSamplesMask;
    const uint frame = fc.frameIndex_ & 0xFFFFu;
    const uint age = (frame - (v.history_ >> 16)) & 0xFFFFu;
    const uint probe = entry * RtProbeWords;

    if (v.count_ == 0u)
    {
        if (age > fc.giMaxAge_)
        {
            GiKeys[entry] = 0;
            GiValues[entry] = (RtCacheValueGPUData)0;
            GiProbes[probe + RtProbeBest] = 0u;
            GiProbes[probe + RtProbeChange] = 0u;
            GiProbes[probe + RtProbeState] = RtProbeNone;
            GiStatAdd(RtCacheStatEvicted, bucket);
        }
        else
        {
            if (v.vote_ != 0u)
                GiValues[entry].vote_ = 0u;
            GiStatAdd(RtCacheStatOccupied, bucket);
        }
        return none;
    }

    float tau = GiSlowTau;
    float agility = 0.0f;
    if (fc.giPerCell_ != 0u)
    {
        if (GiProbes[probe + RtProbeState] == RtProbeArmed)
            tau = GiValidatedTau;
        const float change = asfloat(GiProbes[probe + RtProbeChange]);
        GiProbes[probe + RtProbeChange] = 0u;
        if (change >= GiProbeReset)
            samples = 0u;
        agility = min(GiProbeAgility * change, 1.0f);
        const uint best = GiProbes[probe + RtProbeBest];
        if (best != 0u)
        {
            GiProbes[probe + RtProbeBest] = 0u;
            const uint state = GiProbes[probe + RtProbeState];
            const bool settled =
                fc.frameIndex_ - GiProbes[probe + RtProbeBorn] >= GiProbeLifetime;
            const bool brighter =
                asfloat(best & 0xFFFFF000u) > asfloat(GiProbes[probe + RtProbeRadiance]);
            if (state == RtProbeNone ||
                (state == RtProbeArmed && settled &&
                 (brighter || (fc.frameIndex_ + entry) % GiProbeRenewal == 0u)))
            {
                const uint at = RtCacheWorkCandidates +
                                (v.vote_ & (RtCacheCandidateCapacity - 1u)) * RtCacheCandidateWords;
                const float3 P = asfloat(uint3(GiWork[at + 0u], GiWork[at + 1u], GiWork[at + 2u]));
                const float3 N = asfloat(uint3(GiWork[at + 3u], GiWork[at + 4u], GiWork[at + 5u]));
                GiRenewProbe(entry, best, P, N);
            }
        }
    }

    const float3 estimate = float3(v.sum_[0], v.sum_[1], v.sum_[2]) /
                            (RtCacheScale * fc.exposure_ * float(v.count_));
    const float3 previous = float3(v.irradiance_[0], v.irradiance_[1], v.irradiance_[2]);
    const float running = float(v.count_) / float(samples + v.count_);
    const float alpha =
        max(max(running, agility), 1.0f - pow(1.0f - fc.giDt_ / tau, float(age)));
    const float3 slow = lerp(previous, estimate, alpha);
    const uint folded = min(samples + v.count_, GiSamplesMask);
    const float3 filtered = GiNeighbourAverage(uint2(uint(key >> 32), uint(key)), slow,
                                               saturate(float(folded) / GiShowSamples));

    v.irradiance_[0] = slow.x;
    v.irradiance_[1] = slow.y;
    v.irradiance_[2] = slow.z;
    v.filtered_[0] = filtered.x;
    v.filtered_[1] = filtered.y;
    v.filtered_[2] = filtered.z;
    v.history_ = folded | (frame << 16);
    v.sum_[0] = 0u;
    v.sum_[1] = 0u;
    v.sum_[2] = 0u;
    v.count_ = 0u;
    v.vote_ = 0u;
    GiValues[entry] = v;
    GiStatAdd(RtCacheStatOccupied, bucket);
    return none;
}
