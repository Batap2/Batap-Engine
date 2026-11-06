#ifndef BATAP_BRDF_HLSLI
#define BATAP_BRDF_HLSLI

static const float BRDF_PI = 3.14159265358979f;

float3 BrdfF0(float ior, float3 albedo, float metallic)
{
    float f0 = (ior - 1.0f) / (ior + 1.0f);
    return lerp((f0 * f0).xxx, albedo, metallic);
}

float D_GGX(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (BRDF_PI * d * d);
}

// Height-correlated Smith (Heitz 2014) with the 1 / (4 NdotV NdotL) of
// Cook-Torrance folded in: specular = D * Vis * F.
float V_SmithGGXCorrelated(float NdotV, float NdotL, float alpha)
{
    float a2 = alpha * alpha;
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    return 0.5f / max(gv + gl, 1e-5f);
}

float3 F_Schlick(float HdotV, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - HdotV), 5.0f);
}

float3 BrdfEvalSpecular(float NdotV, float NdotL, float NdotH, float HdotV, float alpha, float3 F0)
{
    return D_GGX(NdotH, alpha) * V_SmithGGXCorrelated(NdotV, NdotL, alpha) * F_Schlick(HdotV, F0);
}

static const float EonC1 = 0.5f - 2.0f / (3.0f * BRDF_PI);
static const float EonC2 = 2.0f / 3.0f - 28.0f / (15.0f * BRDF_PI);

float EonFujiiAlbedo(float mu, float r)
{
    float m = 1.0f - mu;
    float gOverPi =
        m * (0.0571085289f + m * (0.491881867f + m * (-0.332181442f + m * 0.0714429953f)));
    return (1.0f + r * gOverPi) / (1.0f + EonC1 * r);
}

float3 EonMultiAlbedo(float3 rho, float avg)
{
    return rho * rho * avg / (1.0f - rho * (1.0f - avg));
}

float3 BrdfEvalDiffuse(float3 rho, float r, float NdotL, float NdotV, float LdotV)
{
    if (r <= 0.0f)
        return rho / BRDF_PI;
    float a = 1.0f / (1.0f + EonC1 * r);
    float s = LdotV - NdotL * NdotV;
    float sOverT = s > 0.0f ? s / max(max(NdotL, NdotV), 1e-5f) : s;
    float avg = a * (1.0f + EonC2 * r);
    float lostV = max(1.0f - EonFujiiAlbedo(NdotV, r), 0.0f);
    float lostL = max(1.0f - EonFujiiAlbedo(NdotL, r), 0.0f);
    float multi = lostV * lostL / max(1.0f - avg, 1e-7f);
    return (rho * (a * (1.0f + r * sOverT)) + EonMultiAlbedo(rho, avg) * multi) / BRDF_PI;
}

float3 BrdfDiffuseAlbedo(float3 rho, float r, float NdotV)
{
    if (r <= 0.0f)
        return rho;
    float avg = (1.0f + EonC2 * r) / (1.0f + EonC1 * r);
    float e = EonFujiiAlbedo(NdotV, r);
    return rho * e + EonMultiAlbedo(rho, avg) * (1.0f - e);
}

static const float RegolithOppositionAmplitude = 1.0f;
static const float RegolithOppositionWidth = 0.07f;

float3 RegolithGamma(float3 rho)
{
    return (1.0f - rho) / (1.0f + 2.0f * rho);
}

float3 RegolithH(float x, float3 gamma)
{
    float3 w = 1.0f - gamma * gamma;
    float3 r0 = (1.0f - gamma) / (1.0f + gamma);
    x = max(x, 1e-4f);
    return 1.0f / (1.0f - w * x * (r0 + (1.0f - 2.0f * r0 * x) * (0.5f * log((1.0f + x) / x))));
}

float3 BrdfEvalRegolith(float3 rho, float NdotL, float NdotV, float LdotV)
{
    float3 gamma = RegolithGamma(rho);
    float tanHalfPhase = sqrt(max((1.0f - LdotV) / max(1.0f + LdotV, 1e-4f), 0.0f));
    float opposition =
        RegolithOppositionAmplitude / (1.0f + tanHalfPhase / RegolithOppositionWidth);
    float3 multiple = RegolithH(NdotL, gamma) * RegolithH(NdotV, gamma);
    return (1.0f - gamma * gamma) / (4.0f * BRDF_PI) * (opposition + multiple) /
           max(NdotL + NdotV, 1e-4f);
}

float3 BrdfRegolithAlbedo(float3 rho, float NdotV)
{
    float3 gamma = RegolithGamma(rho);
    return 1.0f - gamma * RegolithH(NdotV, gamma);
}

float G1_GGX(float cosTheta, float alpha)
{
    float a2 = alpha * alpha;
    return 2.0f * cosTheta / (cosTheta + sqrt(a2 + (1.0f - a2) * cosTheta * cosTheta));
}

float3 BrdfSampleVndf(float3 V, float alpha, float2 u)
{
    float3 Vs = normalize(float3(V.xy * alpha, V.z));
    float phi = 2.0f * BRDF_PI * u.x;
    float z = (1.0f - u.y) * (1.0f + Vs.z) - Vs.z;
    float s = sqrt(saturate(1.0f - z * z));
    float3 h = float3(s * cos(phi), s * sin(phi), z) + Vs;
    return normalize(float3(h.xy * alpha, h.z));
}

// Density, over directions, of the L that BrdfSampleVndf's H reflects V into.
float BrdfPdfVndf(float3 V, float3 H, float alpha)
{
    return dot(V, H) <= 0.0f ? 0.0f : G1_GGX(V.z, alpha) * D_GGX(H.z, alpha) / (4.0f * V.z);
}

#endif
