#include "Brdf.hlsli"
#include "ShaderInterop.h"

[[vk::binding(ThumbnailMaterialsBinding, ThumbnailSet)]]
StructuredBuffer<Material> ThumbnailMaterials;
[[vk::binding(ThumbnailDrawsBinding, ThumbnailSet)]]
StructuredBuffer<ThumbnailDrawGPUData> ThumbnailDraws;

[[vk::binding(SamplerBinding, BindlessSet)]]  SamplerState      g_sampler;
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];

[[vk::push_constant]] ThumbnailPush g_push;

static const float PI = 3.14159265358979f;

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION;
    float3 posVS_    : TEXCOORD0;
    float3 nrmVS_    : TEXCOORD1;
    float2 uv_       : TEXCOORD2;
    float4 tanVS_    : TEXCOORD3;
};

struct ThumbLight
{
    float3 dir_;
    float3 radiance_;
};

float3 SrgbEncode(float3 c)
{
    c = saturate(c);
    return select(c <= 0.0031308f, 12.92f * c, 1.055f * pow(c, 1.0f / 2.4f) - 0.055f);
}

float3 PbrNeutral(float3 c)
{
    const float startCompression = 0.8f - 0.04f;
    const float desaturation = 0.15f;
    const float lowest = min(c.r, min(c.g, c.b));
    c -= lowest < 0.08f ? lowest - 6.25f * lowest * lowest : 0.04f;
    const float peak = max(c.r, max(c.g, c.b));
    if (peak < startCompression)
        return c;
    const float d = 1.0f - startCompression;
    const float newPeak = 1.0f - d * d / (peak + d - startCompression);
    c *= newPeak / peak;
    const float toWhite = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    return lerp(c, newPeak.xxx, toWhite);
}

float4 SampleOr(uint index, float2 uv, float4 fallback)
{
    if (index == InvalidGPUIndex)
        return fallback;
    return g_textures[index].Sample(g_sampler, uv);
}

// ImGui draws the atlas with straight alpha, not premultiplied.
float4 ThumbnailOutput(float3 premultiplied, float coverage)
{
    if (coverage >= 1.0f)
        return float4(premultiplied, 1.0f);
    const float alpha = max(coverage, max(premultiplied.r, max(premultiplied.g, premultiplied.b)));
    return alpha > 0.0f ? float4(premultiplied / alpha, alpha) : 0.0f.xxxx;
}

float4 main(VS_OUTPUT i) : SV_Target
{
    ThumbnailDrawGPUData draw = ThumbnailDraws[g_push.drawIndex_];

    if ((draw.flags_ & ThumbnailImage) != 0u)
    {
        float4 c = g_textures[draw.textureIndex_].Sample(g_sampler, i.uv_);
        if ((draw.flags_ & ThumbnailImageHdr) != 0u)
            return float4(SrgbEncode(PbrNeutral(c.rgb)), 1.0f);
        return c;
    }

    Material mat = ThumbnailMaterials[draw.materialBase_ +
                                      min(g_push.submeshIndex_, draw.materialCount_ - 1u)];
    const float4 albedoSample = SampleOr(mat.albedoTexIdx_, i.uv_, 1.0f.xxxx);
    float3 albedo = mat.albedo.rgb * albedoSample.rgb;
    const float coverage = mat.albedo.a < 1.0f ? saturate(mat.albedo.a * albedoSample.a) : 1.0f;
    if (mat.shadingModel_ == ShadingUnlit)
        return ThumbnailOutput(SrgbEncode(PbrNeutral(albedo * coverage)), coverage);

    float roughness = clamp(mat.roughness * SampleOr(mat.roughnessTexIdx_, i.uv_, 1.0f.xxxx).r,
                            0.05f, 1.0f);
    const bool regolith = mat.shadingModel_ == ShadingRegolith;
    float metallic = saturate(mat.metallic * SampleOr(mat.metallicTexIdx_, i.uv_, 1.0f.xxxx).r);
    if (regolith)
        metallic = 0.0f;
    float diffuseRoughness = saturate(mat.diffuseRoughness);

    float3 Ngeom = normalize(i.nrmVS_);
    float3 N = Ngeom;
    if (mat.normalTexIdx_ != InvalidGPUIndex)
    {
        float3 ns = g_textures[mat.normalTexIdx_].Sample(g_sampler, i.uv_).rgb * 2.0f - 1.0f;
        ns.z = sqrt(saturate(1.0f - dot(ns.xy, ns.xy)));
        float3 T = normalize(i.tanVS_.xyz);
        T = normalize(T - dot(T, Ngeom) * Ngeom);
        float3 B = cross(Ngeom, T) * i.tanVS_.w;
        N = normalize(ns.x * T + ns.y * B + ns.z * Ngeom);
    }

    float3 V = normalize(-i.posVS_);
    float NdotV = max(dot(N, V), 1e-4f);
    float3 F0 = BrdfF0(mat.ior, albedo, metallic);
    // No frame data here: 1 - 0.7 r³ fits the LTC amplitude table's GGX
    // albedo at normal incidence within 0.07.
    float3 energy =
        1.0f + F0 * (1.0f / (1.0f - 0.7f * roughness * roughness * roughness) - 1.0f);
    float3 kd = (1.0f.xxx - F_Schlick(NdotV, F0) * energy) * (1.0f - metallic);

    ThumbLight lights[3];
    lights[0].dir_ = normalize(float3(-0.45f, 0.75f, 0.55f));
    lights[0].radiance_ = 2.6f.xxx;
    lights[1].dir_ = normalize(float3(0.85f, 0.15f, 0.5f));
    lights[1].radiance_ = float3(0.55f, 0.6f, 0.7f);
    lights[2].dir_ = normalize(float3(0.2f, 0.5f, -0.85f));
    lights[2].radiance_ = 0.9f.xxx;

    float3 color = 0.0f.xxx;
    [unroll]
    for (uint l = 0; l < 3; ++l)
    {
        float3 L = lights[l].dir_;
        float NdotL = saturate(dot(N, L));
        if (NdotL <= 0.0f)
            continue;
        float3 H = normalize(L + V);
        float NdotH = saturate(dot(N, H));
        float HdotV = saturate(dot(H, V));
        float3 spec = 0.0f.xxx;
        float3 diffuse;
        if (regolith)
        {
            diffuse = BrdfEvalRegolith(albedo, NdotL, NdotV, dot(L, V));
        }
        else
        {
            spec = BrdfEvalSpecular(NdotV, NdotL, NdotH, HdotV, roughness * roughness, F0) * energy;
            diffuse = kd * BrdfEvalDiffuse(albedo, diffuseRoughness, NdotL, NdotV, dot(L, V));
        }
        color += (diffuse * coverage + spec) * lights[l].radiance_ * NdotL;
    }

    float ambient = lerp(0.22f, 0.42f, N.y * 0.5f + 0.5f);
    if (regolith)
        color += BrdfRegolithAlbedo(albedo, NdotV) * coverage * ambient;
    else
        color += (BrdfDiffuseAlbedo(albedo, diffuseRoughness, NdotV) * (1.0f - metallic) *
                      coverage +
                  F0 * (1.0f - roughness) * 0.5f) *
                 ambient;
    color += mat.emissive * mat.emissiveStrength * 1.5e-5f;
    return ThumbnailOutput(SrgbEncode(PbrNeutral(color)), coverage);
}
