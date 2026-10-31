#ifndef BATAP_SKY_HLSLI
#define BATAP_SKY_HLSLI

#include "ShaderInterop.h"

[[vk::binding(AtmospheresBinding, FrameSet)]]
StructuredBuffer<AtmosphereGPUData> AtmosphereBuffer;

static const float SKY_PI = 3.14159265358979f;

struct SkySun
{
    float3 dir;
    float intensity;
    float cosRadius;
    bool valid;
    uint shadowIndex;
};

SkySun SkyNoSun()
{
    SkySun s;
    s.dir = float3(0.0f, 1.0f, 0.0f);
    s.intensity = 0.0f;
    s.cosRadius = 2.0f;
    s.valid = false;
    s.shadowIndex = InvalidGPUIndex;
    return s;
}

SkySun SkySunOf(LightGPUData light, float3 cameraPos)
{
    SkySun s = SkyNoSun();
    if (light.type_ == LightFarPoint)
    {
        float3 toSun = light.pos_ - cameraPos;
        float d = max(length(toSun), 1e-3f);
        s.dir = toSun / d;
        float sinR = saturate(light.sourceRadius_ / d);
        s.cosRadius = light.sourceRadius_ > 0.0f ? sqrt(1.0f - sinR * sinR) : 2.0f;
        s.intensity = light.intensity_;
        s.valid = true;
        s.shadowIndex = light.shadowIndex_;
    }
    else if (light.type_ == LightDirectional)
    {
        s.dir = -normalize(light.direction_);
        s.cosRadius = light.sourceRadius_ > 0.0f ? cos(light.sourceRadius_) : 2.0f;
        s.intensity = light.intensity_;
        s.valid = true;
        s.shadowIndex = light.shadowIndex_;
    }
    return s;
}

float2 SkySphereHits(float3 p, float3 d, float r)
{
    float b = dot(p, d);
    float3 q = p - b * d;
    float disc = r * r - dot(q, q);
    if (disc < 0.0f)
        return float2(-1.0f, -1.0f);
    float s = sqrt(disc);
    return float2(-b - s, -b + s);
}

float2 SkyDensities(AtmosphereGPUData atm, float3 p)
{
    float h = max(length(p) - atm.planetRadius_, 0.0f);
    return float2(exp(-h / atm.rayleighHeight_), exp(-h / atm.mieHeight_));
}

// Through the planet too, where the density is the ground's: a sun ray that
// grazes it darkens with the length of its chord instead of switching off.
float2 SkyOpticalDepth(AtmosphereGPUData atm, float3 p, float3 d, int steps)
{
    float tEnd = SkySphereHits(p, d, atm.atmosphereRadius_).y;
    if (tEnd <= 0.0f)
        return float2(0.0f, 0.0f);
    float ds = tEnd / steps;
    float2 od = float2(0.0f, 0.0f);
    [loop]
    for (int i = 0; i < steps; ++i)
        od += SkyDensities(atm, p + d * (ds * (float(i) + 0.5f))) * ds;
    return od;
}

// Mie absorbs about a tenth of what it scatters.
float3 SkyTransmittance(AtmosphereGPUData atm, float2 od)
{
    return exp(-(atm.rayleigh_ * od.x + atm.mie_ * 1.1f * od.y));
}

// Sun visibility of a point of air, camera-relative, for the in-scatter. The
// includer defines it over its shadow data; the sky pass has none and keeps
// the air lit.
#ifndef SKY_AIR_VISIBILITY
#define SKY_AIR_VISIBILITY(pRel, sun) 1.0f
#endif

float3 SkyInscatter(AtmosphereGPUData atm, float3 viewer, float3 dir, float tMax, SkySun sun,
                    int viewSteps, int sunSteps, bool shadowed, out float3 transmittance)
{
    transmittance = float3(1.0f, 1.0f, 1.0f);
    float2 hits = SkySphereHits(viewer, dir, atm.atmosphereRadius_);
    if (hits.y <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);
    float tStart = max(hits.x, 0.0f);
    float tEnd = min(hits.y, tMax);
    if (tEnd <= tStart)
        return float3(0.0f, 0.0f, 0.0f);

    float mu = dot(dir, sun.dir);
    float g = atm.mieAnisotropy_;
    float phaseR = 3.0f / (16.0f * SKY_PI) * (1.0f + mu * mu);
    float phaseM = 3.0f / (8.0f * SKY_PI) * (1.0f - g * g) * (1.0f + mu * mu) /
                   ((2.0f + g * g) * pow(1.0f + g * g - 2.0f * g * mu, 1.5f));

    float ds = (tEnd - tStart) / viewSteps;
    float3 sumR = float3(0.0f, 0.0f, 0.0f);
    float3 sumM = float3(0.0f, 0.0f, 0.0f);
    float2 od = float2(0.0f, 0.0f);
    // The relief's shadow on the air: without it the Mie peak glows on a hill
    // the sun is behind. Visibility is a step function of t, read at the step
    // ends; a step whose ends differ is bisected to the crossing, so its lit
    // fraction is continuous. Read at the midpoints alone, the haze would come
    // in as many levels as there are steps, and band.
    float visEnd = shadowed ? SKY_AIR_VISIBILITY(dir * tStart, sun) : 1.0f;
    [loop]
    for (int i = 0; i < viewSteps; ++i)
    {
        float t0 = tStart + ds * float(i);
        float t1 = t0 + ds;
        float3 p = viewer + dir * (t0 + ds * 0.5f);
        float2 dens = SkyDensities(atm, p) * ds;
        od += dens;
        float3 att = SkyTransmittance(atm, od + SkyOpticalDepth(atm, p, sun.dir, sunSteps));
        if (shadowed)
        {
            float visStart = visEnd;
            visEnd = SKY_AIR_VISIBILITY(dir * t1, sun);
            float lit = visStart;
            if (visStart != visEnd)
            {
                float lo = t0;
                float hi = t1;
                [unroll]
                for (int b = 0; b < 4; ++b)
                {
                    float mid = 0.5f * (lo + hi);
                    if (SKY_AIR_VISIBILITY(dir * mid, sun) == visStart)
                        lo = mid;
                    else
                        hi = mid;
                }
                float crossing = (0.5f * (lo + hi) - t0) / ds;
                lit = visStart * crossing + visEnd * (1.0f - crossing);
            }
            att *= lit;
        }
        sumR += att * dens.x;
        sumM += att * dens.y;
    }
    transmittance = SkyTransmittance(atm, od);
    return (sun.intensity * atm.boost_) * (sumR * atm.rayleigh_ * phaseR + sumM * atm.mie_ * phaseM);
}

float3 SkyComposite(uint count, float3 cameraPos, float3 dir, SkySun sun, float3 background,
                    bool withSun)
{
    float3 through = float3(1.0f, 1.0f, 1.0f);
    float3 scattered = float3(0.0f, 0.0f, 0.0f);
    [loop]
    for (uint i = 0; i < count; ++i)
    {
        AtmosphereGPUData atm = AtmosphereBuffer[i];
        float3 viewer = cameraPos - atm.center_;
        float2 ground = SkySphereHits(viewer, dir, atm.planetRadius_);
        bool hitsGround = ground.x > 0.0f;
        float3 t;
        float3 s =
            SkyInscatter(atm, viewer, dir, hitsGround ? ground.x : 1e30f, sun, 16, 6, false, t);
        if (hitsGround)
        {
            float3 pg = viewer + dir * ground.x;
            float lit = max(dot(normalize(pg), sun.dir), 0.0f);
            float3 sunT = SkyTransmittance(atm, SkyOpticalDepth(atm, pg, sun.dir, 6));
            s += atm.groundAlbedo_ * (sun.intensity * atm.boost_ / SKY_PI) * lit * sunT * t;
            t = float3(0.0f, 0.0f, 0.0f);
        }
        scattered += s;
        through *= t;
    }

    float3 color = background * through + scattered;
    float mu = dot(dir, sun.dir);
    if (withSun && sun.valid && mu > sun.cosRadius)
    {
        float limb = smoothstep(sun.cosRadius, lerp(sun.cosRadius, 1.0f, 0.3f), mu);
        color += (sun.intensity * 1.5f) * through * limb;
    }
    return color;
}

float3 SkyAerial(uint count, float3 cameraPos, float3 posRel, SkySun sun, float3 color)
{
    float dist = length(posRel);
    if (dist < 1e-4f)
        return color;
    float3 dir = posRel / dist;
    [loop]
    for (uint i = 0; i < count; ++i)
    {
        AtmosphereGPUData atm = AtmosphereBuffer[i];
        float3 t;
        float3 s = SkyInscatter(atm, cameraPos - atm.center_, dir, dist, sun, 8, 4, true, t);
        color = color * t + s;
    }
    return color;
}

float3 SkySunTransmittance(uint count, float3 worldPos, float3 sunDir)
{
    float3 through = float3(1.0f, 1.0f, 1.0f);
    [loop]
    for (uint i = 0; i < count; ++i)
    {
        AtmosphereGPUData atm = AtmosphereBuffer[i];
        through *= SkyTransmittance(atm, SkyOpticalDepth(atm, worldPos - atm.center_, sunDir, 4));
    }
    return through;
}

float3 SkyBackground(SkyboxGPUData sky, float3 dir)
{
    if (sky.mode == 1u)
        return sky.color1.rgb;
    float hw = max(sky.horizonWidth, 0.001f);
    float tUp = smoothstep(0.0f, hw, dir.y);
    float tDown = smoothstep(0.0f, hw, -dir.y);
    float3 color = lerp(sky.color2.rgb, sky.color1.rgb, tUp);
    return lerp(color, sky.color3.rgb, tDown);
}

#endif
