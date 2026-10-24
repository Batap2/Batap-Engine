#include "RtHit.hlsli"

float3 GiStarIrradiance(float3 P, float3 N)
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    float3 E = float3(0.0f, 0.0f, 0.0f);
    [loop]
    for (uint lightIndex = 0; lightIndex < g_draw.lightCount_; ++lightIndex)
    {
        LightGPUData light = LightBuffer[lightIndex];
        if (light.type_ != LightFarPoint && light.type_ != LightDirectional)
            continue;
        float3 L;
        float dist = ShadowRayFar;
        float rangeAtt = 1.0f;
        if (light.type_ == LightDirectional)
            L = -light.direction_;
        else
        {
            const float3 toLight = (light.pos_ - cam.pos_) - P;
            dist = length(toLight);
            if (dist > light.radius_ || dist <= 0.0001f)
                continue;
            L = toLight / dist;
            rangeAtt = pow(saturate(1.0f - dist / light.radius_), max(light.falloff_, 0.0001f));
        }
        const float NdotL = saturate(dot(N, L));
        if (NdotL <= 0.0f)
            continue;
        E += light.color_ * (rangeAtt * light.intensity_ * NdotL) * TraceShadowRay(P, N, L, dist);
    }
    return E;
}

float3 GiCosineDirection(float3 N, float u1, float u2)
{
    const float3 helper = abs(N.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 T = normalize(cross(helper, N));
    const float3 B = cross(N, T);
    const float r = sqrt(u1);
    const float phi = 2.0f * PI * u2;
    return T * (r * cos(phi)) + B * (r * sin(phi)) + N * sqrt(max(1.0f - u1, 0.0f));
}

float4 main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0) : SV_Target
{
    const FrameGPUData fc = FrameBuffer[0];
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const uint2 pixel = uint2(svpos.xy);
    uint seed = GiHash(pixel.x + GiHash(pixel.y + GiHash(fc.frameIndex_)));

    const float2 jitter = (float2(GiRand(seed), GiRand(seed)) - 0.5f) *
                          (2.0f / float2(max(fc.giWidth_, 1u), max(fc.giHeight_, 1u)));
    const float2 sample = ndc + jitter;

    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, 0.0f);
    ray.Direction = normalize((sample.x / cam.proj_[0][0]) * cam.right_ +
                              (sample.y / cam.proj_[1][1]) * cam.up_ + cam.fwd_);
    ray.TMin = cam.znear_;
    ray.TMax = RtHitFar;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(Tlas, RAY_FLAG_NONE, TlasMaskShadowCaster | TlasMaskNoShadow, ray);
    while (query.Proceed())
    {
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    const RtHit first = RtResolveHit(query, ray.Direction);

    const uint2 key = GiCacheKey(first.gi_, GiLevel(length(first.position_)));
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

    RayDesc bounce;
    bounce.Direction = GiCosineDirection(first.normal_, GiRand(seed), GiRand(seed));
    bounce.Origin = first.position_ +
                    first.normal_ * (ShadowRayOffset + ShadowRayOffsetPerMetre * length(first.position_));
    bounce.TMin = 0.0f;
    bounce.TMax = RtHitFar;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> bounceQuery;
    bounceQuery.TraceRayInline(Tlas, RAY_FLAG_NONE, TlasMaskShadowCaster | TlasMaskNoShadow, bounce);
    while (bounceQuery.Proceed())
    {
    }

    float3 irradiance;
    if (bounceQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        irradiance = g_draw.skyboxCount_ != 0 ? PI * SampleSky(bounce.Direction, 0.0f)
                                              : float3(0.0f, 0.0f, 0.0f);
    else
    {
        const RtHit second = RtResolveHit(bounceQuery, bounce.Direction);
        irradiance = second.shadingModel_ == ShadingUnlit
                         ? PI * second.albedo_
                         : second.albedo_ * GiStarIrradiance(second.position_, second.normal_);
    }
    GiAccumulate(slot, irradiance);
    return float4(irradiance, 1.0f);
}
