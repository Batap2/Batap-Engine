#ifndef BATAP_GIPATH_HLSLI
#define BATAP_GIPATH_HLSLI

#include "RtHit.hlsli"

[[vk::binding(RtCacheWorkBinding, RayTracingSet)]] RWStructuredBuffer<uint> GiWork;
[[vk::binding(RtCacheProbesBinding, RayTracingSet)]] RWStructuredBuffer<uint> GiProbes;

static const float GiValidatedTau = 4.0f;
static const float GiProbeReset = 0.5f;
static const float GiProbeAgility = 30.0f;
static const float GiProbeSignificance = 0.25f;
static const uint GiProbeRenewal = 64u;
static const uint GiProbeLifetime = 4u;

float GiLuminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 GiRayOrigin(float3 P, float3 N)
{
    return P + N * (ShadowRayOffset + ShadowRayOffsetPerMetre * length(P));
}

uint GiCellRaySeed(uint slot, uint ray, uint sampleIndex)
{
    return GiHash(slot + GiHash(ray + GiHash(sampleIndex)));
}

uint GiOctPack(float3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 p = n.xy;
    if (n.z < 0.0f)
        p = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
    const uint2 q = uint2(round(saturate(p * 0.5f + 0.5f) * 65535.0f));
    return q.x | (q.y << 16);
}

float3 GiOctUnpack(uint v)
{
    const float2 p = float2(v & 0xFFFFu, v >> 16) / 65535.0f * 2.0f - 1.0f;
    float3 n = float3(p, 1.0f - abs(p.x) - abs(p.y));
    const float t = saturate(-n.z);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return normalize(n);
}

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

bool GiCameraHit(float2 sample, out RtHit hit)
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
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
    hit = (RtHit)0;
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return false;
    hit = RtResolveHit(query, ray.Direction);
    return true;
}

float3 GiPathIrradiance(RtHit first, float2 firstU, inout uint seed, uint bucket,
                        out float firstDirect)
{
    const FrameGPUData fc = FrameBuffer[0];
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    float3 throughput = float3(1.0f, 1.0f, 1.0f);
    RtHit hit = first;
    firstDirect = 0.0f;
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
        const float3 direct = throughput * GiDirectIrradiance(hit.position_, hit.normal_, seed);
        irradiance += direct;
        if (bounce == 0u)
            firstDirect = GiLuminance(direct);
        if (bounce + 1 == fc.giBounces_)
        {
            const uint tail = GiSlotFind(GiCacheKey(hit.gi_, GiLevel(length(hit.position_))));
            if (tail != GiNoSlot)
                irradiance += throughput * GiIrradiance(tail);
            else
                GiStatAdd(RtCacheStatTailMiss, bucket);
        }
    }
    return irradiance;
}

float GiProbeRadiance(float3 origin, float3 direction, uint seed)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = RtHitFar;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(Tlas, RAY_FLAG_NONE, TlasMaskShadowCaster | TlasMaskNoShadow, ray);
    while (query.Proceed())
    {
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return g_draw.skyboxCount_ != 0 ? GiLuminance(PI * SampleSky(direction, 0.0f)) : 0.0f;
    const RtHit hit = RtResolveHit(query, direction);
    if (hit.shadingModel_ == ShadingUnlit)
        return GiLuminance(PI * hit.albedo_);
    return GiLuminance(hit.albedo_ * GiDirectIrradiance(hit.position_, hit.normal_, seed));
}

void GiValidateProbe(uint slot)
{
    const uint base = slot * RtProbeWords;
    const uint state = GiProbes[base + RtProbeState];
    if (state == RtProbeNone)
        return;
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const uint packed = GiProbes[base + RtProbeDirection];
    const float3 then = asfloat(uint3(GiProbes[base + RtProbeCamera],
                                      GiProbes[base + RtProbeCamera + 1u],
                                      GiProbes[base + RtProbeCamera + 2u]));
    const float3 origin = asfloat(uint3(GiProbes[base + RtProbeOrigin],
                                        GiProbes[base + RtProbeOrigin + 1u],
                                        GiProbes[base + RtProbeOrigin + 2u])) +
                          (then - cam.pos_);
    const float now = GiProbeRadiance(origin, GiOctUnpack(packed), GiHash(packed ^ slot));
    if (state == RtProbeArmed)
    {
        const float before = asfloat(GiProbes[base + RtProbeRadiance]);
        const float high = max(before, now);
        if (high > GiProbeSignificance * GiLuminance(GiIrradiance(slot)))
            GiProbes[base + RtProbeChange] = asuint(abs(now - before) / high);
    }
    GiProbes[base + RtProbeRadiance] = asuint(now);
    GiProbes[base + RtProbeState] = RtProbeArmed;
}

void GiRenewProbe(uint slot, uint best, float3 P, float3 N)
{
    const FrameGPUData fc = FrameBuffer[0];
    const uint base = slot * RtProbeWords;
    const uint pass = best >> 9 & 7u;
    const uint ray = best & 511u;
    uint seed = GiCellRaySeed(slot, ray, fc.frameIndex_ * fc.giPasses_ + pass);
    const float2 firstU = float2(GiRand(seed), GiRand(seed));
    const float3 origin = GiRayOrigin(P, N);
    const float3 camera = CameraInstancebuffer[g_draw.cameraIndex_].pos_;
    GiProbes[base + RtProbeDirection] = GiOctPack(GiCosineDirection(N, firstU.x, firstU.y));
    GiProbes[base + RtProbeOrigin] = asuint(origin.x);
    GiProbes[base + RtProbeOrigin + 1u] = asuint(origin.y);
    GiProbes[base + RtProbeOrigin + 2u] = asuint(origin.z);
    GiProbes[base + RtProbeCamera] = asuint(camera.x);
    GiProbes[base + RtProbeCamera + 1u] = asuint(camera.y);
    GiProbes[base + RtProbeCamera + 2u] = asuint(camera.z);
    GiProbes[base + RtProbeState] = RtProbeFresh;
    GiProbes[base + RtProbeBorn] = fc.frameIndex_;
}

#endif
