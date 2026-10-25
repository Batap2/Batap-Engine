#include "RtHit.hlsli"

float3 GiRectIrradiance(LightGPUData light, float3 P, float3 N, inout uint seed)
{
    const RectFace face = RectLightFace(light, P);
    if (face.window_ <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);
    const float3 helper = abs(N.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3x3 identity = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    const float formFactor =
        LtcEvaluate(N, normalize(cross(helper, N)), P, identity, face.corners_);
    if (formFactor <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);

    float visible = 1.0f;
    if (light.shadowIndex_ != InvalidGPUIndex)
    {
        const float3 q = face.corners_[1] + (face.corners_[0] - face.corners_[1]) * GiRand(seed) +
                         (face.corners_[2] - face.corners_[1]) * GiRand(seed);
        const float3 toQ = q - P;
        const float dist = length(toQ);
        const float3 L = toQ / max(dist, 1e-6f);
        if (dot(N, L) > 0.0f)
            visible = TraceShadowRay(P, N, L, dist);
    }
    return light.color_ * (PI * light.intensity_ * face.window_ * formFactor * visible);
}

float3 GiDirectIrradiance(float3 P, float3 N, inout uint seed)
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    float3 E = float3(0.0f, 0.0f, 0.0f);
    [loop]
    for (uint lightIndex = 0; lightIndex < g_draw.lightCount_; ++lightIndex)
    {
        LightGPUData light = LightBuffer[lightIndex];
        light.pos_ -= cam.pos_;
        if (light.type_ == LightRect)
        {
            E += GiRectIrradiance(light, P, N, seed);
            continue;
        }
        float3 L;
        float dist;
        float attenuation;
        if (!LightIncidence(light, P, L, dist, attenuation))
            continue;
        const float NdotL = saturate(dot(N, L));
        if (NdotL <= 0.0f)
            continue;
        float visible = 1.0f;
        if (light.shadowIndex_ != InvalidGPUIndex)
            visible =
                TraceShadowRay(P, N, L, light.type_ == LightDirectional ? ShadowRayFar : dist);
        E += light.color_ * (attenuation * light.intensity_ * NdotL * visible);
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
    const uint sampleIndex = fc.frameIndex_ * fc.giPasses_ + g_draw.instanceIndex_;
    uint seed = GiHash(pixel.x + GiHash(pixel.y + GiHash(sampleIndex)));

    const uint2 pixelRotation = uint2(GiHash(pixel.x * 7919u + pixel.y), GiHash(pixel.y * 104729u + pixel.x + 1u));
    const float2 jitterU = GiStratified ? GiR2(sampleIndex, pixelRotation)
                                        : float2(GiRand(seed), GiRand(seed));
    const float2 jitter = (jitterU - 0.5f) *
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

    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    float3 throughput = float3(1.0f, 1.0f, 1.0f);
    RtHit hit = first;

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

    [loop]
    for (uint bounce = 0; bounce < fc.giBounces_; ++bounce)
    {
        RayDesc ray2;
        ray2.Direction = bounce == 0u ? GiCosineDirection(hit.normal_, firstU.x, firstU.y)
                                      : GiCosineDirection(hit.normal_, GiRand(seed), GiRand(seed));
        ray2.Origin = hit.position_ + hit.normal_ * (ShadowRayOffset +
                                                      ShadowRayOffsetPerMetre * length(hit.position_));
        ray2.TMin = 0.0f;
        ray2.TMax = RtHitFar;
        RayQuery<RAY_FLAG_FORCE_OPAQUE> bounceQuery;
        bounceQuery.TraceRayInline(Tlas, RAY_FLAG_NONE, TlasMaskShadowCaster | TlasMaskNoShadow, ray2);
        while (bounceQuery.Proceed())
        {
        }
        if (bounceQuery.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        {
            if (g_draw.skyboxCount_ != 0)
                irradiance += throughput * (PI * SampleSky(ray2.Direction, 0.0f));
            break;
        }
        hit = RtResolveHit(bounceQuery, ray2.Direction);
        if (hit.shadingModel_ == ShadingUnlit)
        {
            irradiance += throughput * (PI * hit.albedo_);
            break;
        }
        throughput *= hit.albedo_;
        irradiance += throughput * GiDirectIrradiance(hit.position_, hit.normal_, seed);
        if (bounce + 1 == fc.giBounces_)
        {
            const uint tail = GiSlotFind(GiCacheKey(hit.gi_, GiLevel(length(hit.position_))));
            if (tail != GiNoSlot)
                irradiance += throughput * GiIrradiance(tail);
            else
                GiStatAdd(RtCacheStatTailMiss, bucket);
        }
    }
    GiAccumulate(slot, irradiance, fc.exposure_);
    return float4(irradiance, 1.0f);
}
