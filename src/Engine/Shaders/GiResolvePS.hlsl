#include "Lighting.hlsli"

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData fc = FrameBuffer[0];
    const uint2 pixel = uint2(svpos.xy);
    const uint entry = pixel.y * RtCacheResolveWidth + pixel.x;
    const uint bucket = entry & (RtCacheStatsBuckets - 1u);
    const float4 none = float4(0.0f, 0.0f, 0.0f, 0.0f);

    if (GiKeys[entry] == 0)
        return none;

    RtCacheValueGPUData v = GiValues[entry];
    uint samples = v.history_ & 0xFFFFu;
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

    const float3 estimate = float3(v.sum_[0], v.sum_[1], v.sum_[2]) / (RtCacheScale * float(v.count_));
    float3 value = float3(v.irradiance_[0], v.irradiance_[1], v.irradiance_[2]);

    if (v.count_ >= GiChangeMinSamples)
    {
        const float a = GiLuma(estimate);
        const float b = GiLuma(value);
        if (abs(a - b) > GiChangeRatio * max(a, b) + GiChangeFloor)
            samples = min(samples, v.count_);
    }
    const float floorAlpha = 1.0f - pow(1.0f - fc.giAlphaMin_, float(age));
    const float alpha = max(float(v.count_) / float(samples + v.count_), floorAlpha);
    value = lerp(value, estimate, alpha);

    v.irradiance_[0] = value.x;
    v.irradiance_[1] = value.y;
    v.irradiance_[2] = value.z;
    v.history_ = min(samples + v.count_, 0xFFFFu) | (frame << 16);
    v.sum_[0] = 0u;
    v.sum_[1] = 0u;
    v.sum_[2] = 0u;
    v.count_ = 0u;
    GiValues[entry] = v;
    GiStatAdd(RtCacheStatOccupied, bucket);
    return none;
}
