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
