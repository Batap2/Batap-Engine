#ifndef BATAP_GLINTS_HLSLI
#define BATAP_GLINTS_HLSLI

static const float GlintInvPi = 0.318309886f;
static const float GlintBinPerAlpha = 0.25f;
static const float GlintPoissonBelow = 6.0f;
static const float GlintSmoothAbove = 1e4f;
static const uint GlintMaxLevel = 20u;

uint GlintHash(uint4 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    v ^= v >> 16u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    return v.x ^ v.y ^ v.z ^ v.w;
}

float GlintErfInv(float x)
{
    float w = -log(max((1.0f - x) * (1.0f + x), 1e-30f));
    float p;
    if (w < 5.0f)
    {
        w -= 2.5f;
        p = 2.81022636e-08f;
        p = 3.43273939e-07f + p * w;
        p = -3.5233877e-06f + p * w;
        p = -4.39150654e-06f + p * w;
        p = 0.00021858087f + p * w;
        p = -0.00125372503f + p * w;
        p = -0.00417768164f + p * w;
        p = 0.246640727f + p * w;
        p = 1.50140941f + p * w;
    }
    else
    {
        w = sqrt(w) - 3.0f;
        p = -0.000200214257f;
        p = 0.000100950558f + p * w;
        p = 0.00134934322f + p * w;
        p = -0.00367342844f + p * w;
        p = 0.00573950773f + p * w;
        p = -0.0076224613f + p * w;
        p = 0.00943887047f + p * w;
        p = 1.00167406f + p * w;
        p = 2.83297682f + p * w;
    }
    return p * x;
}

float GlintCount(float n, float mean, float u)
{
    if (mean < GlintPoissonBelow)
    {
        float k = 0.0f;
        float term = exp(-mean);
        float cdf = term;
        [loop]
        while (u > cdf && k < n && k < 4.0f * GlintPoissonBelow)
        {
            k += 1.0f;
            term *= mean / k;
            cdf += term;
        }
        return k;
    }
    const float sigma = sqrt(mean * saturate(1.0f - mean / n));
    return clamp(mean + sigma * 1.41421356f * GlintErfInv(2.0f * u - 1.0f), 0.0f, n);
}

float GlintCellRatio(int2 cell, uint level, float n, float2 bins, float alpha, float binWidth)
{
    const float2 q = bins - 0.5f;
    const float2 first = floor(q);
    const float2 f = q - first;
    float ratio = 0.0f;
    [unroll]
    for (uint j = 0; j < 2u; ++j)
    {
        [unroll]
        for (uint i = 0; i < 2u; ++i)
        {
            const float2 bin = first + float2(i, j);
            const float2 slope = (bin + 0.5f) * binWidth;
            const float t = 1.0f + dot(slope, slope) / (alpha * alpha);
            const float mean = n * binWidth * binWidth * GlintInvPi / (alpha * alpha * t * t);
            const int2 b = int2(bin);
            const uint h =
                GlintHash(uint4(asuint(cell.x), asuint(cell.y), asuint(b.x) ^ (level << 24u), asuint(b.y)));
            const float k = GlintCount(n, mean, (float(h >> 8u) + 0.5f) * (1.0f / 16777216.0f));
            const float2 w = lerp(1.0f - f, f, float2(i, j));
            ratio += w.x * w.y * k / max(mean, 1e-20f);
        }
    }
    return ratio;
}

float GlintLevelRatio(float2 uv, float density, uint level, float2 bins, float alpha, float binWidth)
{
    const float2 g = uv * (density / float(1u << level)) - 0.5f;
    const float2 first = floor(g);
    const float2 f = g - first;
    const float n = exp2(2.0f * float(level));
    float ratio = 0.0f;
    [unroll]
    for (uint j = 0; j < 2u; ++j)
    {
        [unroll]
        for (uint i = 0; i < 2u; ++i)
        {
            const float2 w = lerp(1.0f - f, f, float2(i, j));
            ratio += w.x * w.y *
                     GlintCellRatio(int2(first) + int2(i, j), level, n, bins, alpha, binWidth);
        }
    }
    return ratio;
}

float GlintRatio(float2 uv, float2 uvDx, float2 uvDy, float3 T, float3 N, float3 H, float alpha,
                 float density)
{
    const float hn = dot(H, N);
    if (hn <= 1e-4f)
        return 1.0f;
    const float3 B = cross(N, T);
    const float binWidth = GlintBinPerAlpha * alpha;
    const float2 bins = float2(dot(H, T), dot(H, B)) / (hn * binWidth);

    const float lx = length(uvDx) * density;
    const float ly = length(uvDy) * density;
    const float major = max(lx, ly);
    const float minor = max(min(lx, ly), 0.25f * major);
    const float lod = clamp(0.5f * log2(max(major * minor, 1e-20f)), 0.0f, float(GlintMaxLevel));
    const uint level = min(uint(lod), GlintMaxLevel - 1u);
    const float blend = lod - float(level);
    if (exp2(2.0f * float(level)) * GlintBinPerAlpha * GlintBinPerAlpha * GlintInvPi >
        GlintSmoothAbove)
        return 1.0f;

    float ratio = GlintLevelRatio(uv, density, level, bins, alpha, binWidth);
    if (blend > 0.0f)
        ratio = lerp(ratio, GlintLevelRatio(uv, density, level + 1u, bins, alpha, binWidth), blend);
    return ratio;
}

#endif
