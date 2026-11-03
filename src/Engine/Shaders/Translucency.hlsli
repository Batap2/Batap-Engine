#ifndef BATAP_TRANSLUCENCY_HLSLI
#define BATAP_TRANSLUCENCY_HLSLI

// The accumulation target is fp16: raising RadianceMax or WeightMax overflows
// it, lowering WeightMin underflows coverage x weight.
static const float TranslucentRadianceMax = 256.0f;
static const float TranslucentWeightMin = 1.0f / 64.0f;
static const float TranslucentWeightMax = 16.0f;
static const float TranslucentWeightDistance = 8.0f;

struct TranslucentOutput
{
    float4 accum_  : SV_Target0;
    float  reveal_ : SV_Target1;
};

float TranslucentDepthWeight(float distance)
{
    const float near = TranslucentWeightDistance / max(distance, 1e-3f);
    return clamp(near * near, TranslucentWeightMin, TranslucentWeightMax);
}

TranslucentOutput TranslucentAccumulate(float3 premultiplied, float coverage, float distance)
{
    const float weight = coverage * TranslucentDepthWeight(distance);
    TranslucentOutput o;
    o.accum_ = float4(min(premultiplied, TranslucentRadianceMax) * weight, coverage * weight);
    o.reveal_ = coverage;
    return o;
}

#endif
