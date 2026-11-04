#ifndef BATAP_BILLBOARD_HLSLI
#define BATAP_BILLBOARD_HLSLI

#include "Lighting.hlsli"

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION;
    float2 uv_       : TEXCOORD0;
    float4 tint_     : TEXCOORD1;
    float3 posRel_   : TEXCOORD2;
    float3 normalWS_ : TEXCOORD3;
    nointerpolation uint materialIdx_ : TEXCOORD4;
    nointerpolation uint textureIdx_  : TEXCOORD5;
};

Surface BillboardSurface(VS_OUTPUT i, out Material mat, out float alpha)
{
    uint safeIdx = (i.materialIdx_ != InvalidGPUIndex) ? i.materialIdx_ : 0u;
    mat = MaterialBuffer[safeIdx];

    uint texIdx = (i.textureIdx_ != InvalidGPUIndex) ? i.textureIdx_ : mat.albedoTexIdx_;
    float4 tex = (texIdx != InvalidGPUIndex) ? g_textures[texIdx].Sample(g_sampler, i.uv_)
                                             : float4(1.f, 1.f, 1.f, 1.f);

    float4 base = tex * mat.albedo * i.tint_;
    alpha = base.a;

    Surface s;
    s.albedo_ = base.rgb;
    s.roughness_ = clamp(mat.roughness, 0.05f, 1.f);
    s.metallic_ = saturate(mat.metallic);
    s.reflectivity_ = mat.reflectivity;
    s.emissive_ = mat.emissive * mat.emissiveStrength;
    s.glintDensity_ = 0.0f;
    s.N_ = normalize(i.normalWS_);
    s.posRel_ = i.posRel_;
    s.cell_ = 0.0f;
    return s;
}

#endif
