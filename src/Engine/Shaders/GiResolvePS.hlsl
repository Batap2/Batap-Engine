#include "Lighting.hlsli"

float3 GiWithLuma(float3 c, float target)
{
    const float l = GiLuma(c);
    return l > 1e-6f ? c * (target / l) : float3(target, target, target);
}

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
    uint samples = v.history_ & GiSamplesMask;
    const uint prevRun = (v.history_ >> GiZeroRunShift) & GiZeroRunMask;
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
    float3 slow = float3(v.irradiance_[0], v.irradiance_[1], v.irradiance_[2]);
    const bool zeroFrame = samples == 0u
                               ? v.sum_[0] == 0u && v.sum_[1] == 0u && v.sum_[2] == 0u
                               : GiLuma(estimate) < GiDarkRatio * GiLuma(slow);
    const uint zeroRun = zeroFrame ? min(prevRun + 1u, GiZeroRunMask) : 0u;
    float fastLuma = v.fastLuma_;
    float variance = v.variance_;
    float zeroFraction = v.zeroFraction_;
    float fastCount = v.fastCount_;
    const float slowRate = fc.giDt_ / GiSlowTau;
    const float fastRate = fc.giDt_ / GiFastTau;

    if (samples == 0u)
    {
        slow = estimate;
        fastLuma = GiLuma(estimate);
        variance = 0.0f;
        zeroFraction = zeroFrame ? 1.0f : 0.0f;
        fastCount = float(v.count_);
    }
    else
    {
        const float running = float(v.count_) / float(samples + v.count_);
        const float alphaSlow = max(running, 1.0f - pow(1.0f - slowRate, float(age)));
        const float alphaFast = max(running, 1.0f - pow(1.0f - fastRate, float(age)));
        const float d = GiLuma(estimate) - GiLuma(slow);
        variance = lerp(variance, d * d, alphaSlow);
        slow = lerp(slow, estimate, alphaSlow);
        fastLuma = lerp(fastLuma, GiLuma(estimate), alphaFast);
        fastCount = lerp(fastCount, float(v.count_), alphaFast);
        // The fraction of empty frames moves only when a run ends, with the
        // whole run at once: updated during the run, it would chase the run
        // and the dark test below would never fire on a light switched off.
        if (!zeroFrame)
        {
            const float frames = float(prevRun + 1u);
            zeroFraction = lerp(zeroFraction, float(prevRun) / frames,
                                1.0f - pow(1.0f - slowRate, frames));
        }

        if (samples >= GiChangeMinSamples)
        {
            const float needed = log(GiFalseResetRate) / log(clamp(zeroFraction, 1e-3f, 0.999f));
            const bool wentDark = float(zeroRun) >= needed && GiLuma(slow) > GiResetFloor;
            const float fastSamples = fastCount * (GiFastTau / fc.giDt_);
            const float sigma = sqrt(max(variance, 0.0f) * fastRate / (2.0f - fastRate));
            const float gap = fastLuma - GiLuma(slow);
            if (wentDark)
            {
                slow = float3(0.0f, 0.0f, 0.0f);
                samples = GiResetSamples;
                GiStatAdd(RtCacheStatReset, bucket);
            }
            else if (fastSamples >= GiChangeMinFastSamples)
            {
                if (abs(gap) > GiResetSigma * sigma + GiResetFloor)
                {
                    slow = GiWithLuma(slow, fastLuma);
                    samples = GiResetSamples;
                    GiStatAdd(RtCacheStatReset, bucket);
                }
                else
                {
                    const float band = GiClampSigma * sigma + GiClampFloor;
                    if (abs(gap) > band)
                        slow = GiWithLuma(slow, fastLuma - sign(gap) * band);
                }
            }
        }
    }

    v.irradiance_[0] = slow.x;
    v.irradiance_[1] = slow.y;
    v.irradiance_[2] = slow.z;
    v.fastLuma_ = fastLuma;
    v.variance_ = variance;
    v.zeroFraction_ = zeroFraction;
    v.fastCount_ = fastCount;
    v.history_ = min(samples + v.count_, GiSamplesMask) | (zeroRun << GiZeroRunShift) | (frame << 16);
    v.sum_[0] = 0u;
    v.sum_[1] = 0u;
    v.sum_[2] = 0u;
    v.count_ = 0u;
    GiValues[entry] = v;
    GiStatAdd(RtCacheStatOccupied, bucket);
    return none;
}
