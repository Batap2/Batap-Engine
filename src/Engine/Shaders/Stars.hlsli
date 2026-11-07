#ifndef BATAP_STARS_HLSLI
#define BATAP_STARS_HLSLI

#include "ShaderInterop.h"

static const float StarCells = 128.0f;
static const float StarMeanCellSolidAngle = 0.5235988f;
static const float StarDimmest = 0.01f;
static const float StarBrightnessSlope = -0.6667f;
static const float StarVisibleLevel = 0.03f;
static const float StarSigmaPixels = 0.7f;
static const float StarBandBoost = 2.0f;
static const float MilkyWayCells = 512.0f;
static const float MilkyWaySigmaPixels = 0.55f;

uint3 StarPcg(uint3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

float3 StarRandom(uint3 v)
{
    return float3(StarPcg(v) >> 8u) * (1.0f / 16777216.0f);
}

void StarFace(float3 d, out uint face, out float2 uv)
{
    float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)
    {
        face = d.x > 0.0f ? 0u : 1u;
        uv = d.yz / a.x;
    }
    else if (a.y >= a.z)
    {
        face = d.y > 0.0f ? 2u : 3u;
        uv = d.xz / a.y;
    }
    else
    {
        face = d.z > 0.0f ? 4u : 5u;
        uv = d.xy / a.z;
    }
}

float3 StarFaceDir(uint face, float2 uv)
{
    float s = (face & 1u) != 0u ? -1.0f : 1.0f;
    float3 d;
    if (face < 2u)
        d = float3(s, uv.x, uv.y);
    else if (face < 4u)
        d = float3(uv.x, s, uv.y);
    else
        d = float3(uv.x, uv.y, s);
    return normalize(d);
}

float StarCellSolidAngle(float2 uv)
{
    return pow(1.0f + dot(uv, uv), -1.5f);
}

float3 StarColour(float t)
{
    if (t < 0.12f)
        return float3(0.68f, 0.78f, 1.0f);
    if (t < 0.45f)
        return float3(0.86f, 0.91f, 1.0f);
    if (t < 0.78f)
        return float3(1.0f, 0.97f, 0.92f);
    if (t < 0.94f)
        return float3(1.0f, 0.84f, 0.66f);
    return float3(1.0f, 0.68f, 0.48f);
}

float StarGradient(uint3 c, float3 f)
{
    return dot(StarRandom(c) * 2.0f - 1.0f, f);
}

float StarPerlin(float3 p)
{
    float3 i = floor(p);
    float3 f = p - i;
    float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    uint3 c = asuint(int3(i));
    float n000 = StarGradient(c, f);
    float n100 = StarGradient(c + uint3(1u, 0u, 0u), f - float3(1.0f, 0.0f, 0.0f));
    float n010 = StarGradient(c + uint3(0u, 1u, 0u), f - float3(0.0f, 1.0f, 0.0f));
    float n110 = StarGradient(c + uint3(1u, 1u, 0u), f - float3(1.0f, 1.0f, 0.0f));
    float n001 = StarGradient(c + uint3(0u, 0u, 1u), f - float3(0.0f, 0.0f, 1.0f));
    float n101 = StarGradient(c + uint3(1u, 0u, 1u), f - float3(1.0f, 0.0f, 1.0f));
    float n011 = StarGradient(c + uint3(0u, 1u, 1u), f - float3(0.0f, 1.0f, 1.0f));
    float n111 = StarGradient(c + uint3(1u, 1u, 1u), f - float3(1.0f, 1.0f, 1.0f));
    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    return lerp(lerp(nx00, nx10, u.y), lerp(nx01, nx11, u.y), u.z);
}

float StarFbm(float3 p, int octaves)
{
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i)
    {
        sum += amp * StarPerlin(p);
        norm += amp;
        p = p * 2.07f + 17.1f;
        amp *= 0.5f;
    }
    return saturate(0.5f + 0.9f * sum / norm);
}

float3 MilkyWayCore(float3 pole)
{
    float3 ref = abs(pole.y) < 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    return normalize(cross(pole, ref));
}

float MilkyWayBand(float3 dir, float3 pole)
{
    float b = dot(dir, pole);
    return exp(-b * b * (1.0f / (2.0f * 0.12f * 0.12f)));
}

float3 MilkyWayDust(SkyboxGPUData sky, float3 dir, float pixel, float density)
{
    uint face;
    float2 uv;
    StarFace(dir, face, uv);
    int2 cell = int2(floor((uv * 0.5f + 0.5f) * MilkyWayCells));
    float sigma = MilkyWaySigmaPixels * pixel;

    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 c = cell + int2(dx, dy);
            if (any(c < 0) || any(c >= int(MilkyWayCells)))
                continue;
            float3 r0 = StarRandom(uint3(uint2(c), face * 2u + 16u));
            if (r0.x >= density)
                continue;
            float3 r1 = StarRandom(uint3(uint2(c), face * 2u + 17u));
            float2 starUv = (float2(c) + 0.1f + 0.8f * r0.yz) * (2.0f / MilkyWayCells) - 1.0f;
            float3 offset = StarFaceDir(face, starUv) - dir;
            float spot = exp(-0.5f * dot(offset, offset) / (sigma * sigma));
            sum += (spot * r1.x * r1.x * r1.x) * StarColour(r1.y);
        }
    }
    return sum;
}

float3 MilkyWay(SkyboxGPUData sky, float3 dir, float pixel, float exposure)
{
    float adapted = smoothstep(0.33f * StarVisibleLevel, StarVisibleLevel,
                               sky.milkyWayIntensity * exposure);
    float b = dot(dir, sky.milkyWayPole);
    if (adapted <= 0.0f || abs(b) > 0.5f)
        return float3(0.0f, 0.0f, 0.0f);

    float bulge = pow(saturate(dot(dir, MilkyWayCore(sky.milkyWayPole)) * 0.5f + 0.5f), 5.0f);
    float width = 0.1f + 0.1f * bulge;
    float band = exp(-b * b / (2.0f * width * width)) * smoothstep(0.5f, 0.4f, abs(b));

    float clouds = smoothstep(0.3f, 0.7f, StarFbm(dir * 4.5f, 5));
    float arms = smoothstep(0.25f, 0.7f, StarFbm(dir * 1.5f + 51.0f, 3));
    float brightness = (0.45f + 0.55f * clouds) * (0.6f + 0.4f * arms);

    float3 warp = float3(StarPerlin(dir * 2.0f + 3.1f), StarPerlin(dir * 2.0f + 7.7f),
                         StarPerlin(dir * 2.0f + 12.9f));
    float dustField = StarFbm(dir * 5.0f + warp * 1.2f, 5);
    float ridge = 1.0f - abs(2.0f * dustField - 1.0f);
    float plane = exp(-b * b / (2.0f * 0.6f * width * 0.6f * width));
    float filaments = smoothstep(0.78f, 0.95f, ridge) * plane;
    float laneCentre = width * (StarFbm(dir * 2.0f + 31.0f, 3) - 0.5f) * 0.6f;
    float ragged = 0.35f * width * (dustField - 0.5f);
    float laneWidth = width * lerp(0.08f, 0.28f, StarFbm(dir * 1.8f + 77.0f, 3));
    float lane = 1.0f - smoothstep(0.3f * laneWidth, laneWidth, abs(b - laneCentre) + ragged);
    lane *= smoothstep(0.3f, 0.6f, StarFbm(dir * 1.2f + 93.0f, 3));
    float clear = 1.0f - saturate(0.6f * filaments + 0.7f * lane);

    float glow = band * brightness * (0.5f + 1.5f * bulge) * clear;
    float density = saturate(0.9f * band * brightness * (0.6f + 0.6f * bulge) * clear);
    float3 tint = lerp(float3(0.8f, 0.86f, 1.0f), float3(1.0f, 0.88f, 0.74f), bulge);

    float3 light = 0.3f * glow * tint + 1.6f * MilkyWayDust(sky, dir, pixel, density);
    return (adapted * sky.milkyWayIntensity) * light;
}

float3 Stars(SkyboxGPUData sky, float3 dir, float pixel, float exposure)
{
    if (sky.starIntensity * exposure < StarVisibleLevel || sky.starCount <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);

    uint face;
    float2 uv;
    StarFace(dir, face, uv);
    int2 cell = int2(floor((uv * 0.5f + 0.5f) * StarCells));
    float sigma = StarSigmaPixels * pixel;
    float perCell = sky.starCount / (6.0f * StarCells * StarCells * StarMeanCellSolidAngle);
    bool band = sky.milkyWayIntensity > 0.0f;

    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 c = cell + int2(dx, dy);
            if (any(c < 0) || any(c >= int(StarCells)))
                continue;
            float3 r0 = StarRandom(uint3(uint2(c), face * 2u));
            float3 r1 = StarRandom(uint3(uint2(c), face * 2u + 1u));
            float2 starUv = (float2(c) + 0.15f + 0.7f * r0.yz) * (2.0f / StarCells) - 1.0f;
            float3 starDir = StarFaceDir(face, starUv);
            float density = perCell * StarCellSolidAngle(starUv);
            if (band)
                density *= 1.0f + StarBandBoost * MilkyWayBand(starDir, sky.milkyWayPole);
            if (r0.x >= density)
                continue;
            float3 offset = starDir - dir;
            float spot = exp(-0.5f * dot(offset, offset) / (sigma * sigma));
            float brightness = min(1.0f, StarDimmest * pow(max(r1.x, 1e-6f), StarBrightnessSlope));
            sum += (brightness * spot) * StarColour(r1.y);
        }
    }
    sum *= sky.starIntensity;

    float shown = max(sum.r, max(sum.g, sum.b)) * exposure;
    return sum * smoothstep(StarVisibleLevel, 2.0f * StarVisibleLevel, shown);
}

#endif
