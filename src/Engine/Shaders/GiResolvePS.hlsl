#include "Lighting.hlsli"

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
    const uint samples = v.history_ & GiSamplesMask;
    const uint frame = fc.frameIndex_ & 0xFFFFu;
    const uint age = (frame - (v.history_ >> 16)) & 0xFFFFu;

    if (v.count_ == 0u)
    {
        if (age > fc.giMaxAge_)
        {
            GiKeys[entry] = 0;
            GiValues[entry] = (RtCacheValueGPUData)0;
            GiStatAdd(RtCacheStatEvicted, bucket);
        }
        else
            GiStatAdd(RtCacheStatOccupied, bucket);
        return none;
    }

    const float3 estimate = float3(v.sum_[0], v.sum_[1], v.sum_[2]) /
                            (RtCacheScale * fc.exposure_ * float(v.count_));
    const float3 previous = float3(v.irradiance_[0], v.irradiance_[1], v.irradiance_[2]);
    const float running = float(v.count_) / float(samples + v.count_);
    const float alpha = max(running, 1.0f - pow(1.0f - fc.giDt_ / GiSlowTau, float(age)));
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
    GiValues[entry] = v;
    GiStatAdd(RtCacheStatOccupied, bucket);
    return none;
}
