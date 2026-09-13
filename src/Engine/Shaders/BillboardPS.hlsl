#include "Lighting.hlsli"

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION;
    float2 uv_       : TEXCOORD0;
    float4 tint_     : TEXCOORD1;
    float3 posWS_    : TEXCOORD2;
    float3 normalWS_ : TEXCOORD3;
    nointerpolation uint materialIdx_ : TEXCOORD4;
    nointerpolation uint textureIdx_  : TEXCOORD5;
};

float4 main(VS_OUTPUT i) : SV_Target
{
    uint safeIdx = (i.materialIdx_ != InvalidGPUIndex) ? i.materialIdx_ : 0u;
    Material mat = MaterialBuffer[safeIdx];

    uint texIdx = (i.textureIdx_ != InvalidGPUIndex) ? i.textureIdx_ : mat.albedoTexIdx_;
    float4 tex = (texIdx != InvalidGPUIndex) ? g_textures[texIdx].Sample(g_sampler, i.uv_)
                                             : float4(1.f, 1.f, 1.f, 1.f);

    float4 base = tex * mat.albedo * i.tint_;

    // Cutout, not blending: blending would need a back-to-front sort per frame.
    if (base.a < 0.5f)
        discard;

    Surface s;
    s.albedo_ = base.rgb;
    s.roughness_ = clamp(mat.roughness, 0.05f, 1.f);
    s.metallic_ = saturate(mat.metallic);
    s.reflectivity_ = mat.reflectivity;
    s.N_ = normalize(i.normalWS_);
    s.posWS_ = i.posWS_;

    return float4(saturate(ShadeSurface(mat.shadingModel_, s)), 1.f);
}
