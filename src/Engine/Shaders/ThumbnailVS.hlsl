#include "ShaderInterop.h"

[[vk::binding(ThumbnailDrawsBinding, ThumbnailSet)]]
StructuredBuffer<ThumbnailDrawGPUData> ThumbnailDraws;

[[vk::push_constant]] ThumbnailPush g_push;

struct VS_INPUT
{
    float3 position_ : POSITION;
    float3 normal_   : NORMAL;
    float2 uv_       : TEXCOORD0;
    float4 tangent_  : TANGENT;
};

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION;
    float3 posVS_    : TEXCOORD0;
    float3 nrmVS_    : TEXCOORD1;
    float2 uv_       : TEXCOORD2;
    float4 tanVS_    : TEXCOORD3;
};

VS_OUTPUT main(VS_INPUT input)
{
    ThumbnailDrawGPUData draw = ThumbnailDraws[g_push.drawIndex_];

    VS_OUTPUT o;
    float3 posVS = mul(draw.worldView_, float4(input.position_, 1.0f)).xyz;
    o.posVS_ = posVS;
    o.nrmVS_ = normalize(mul((float3x3)draw.normalView_, input.normal_));
    o.tanVS_ = float4(normalize(mul((float3x3)draw.worldView_, input.tangent_.xyz)),
                      input.tangent_.w);
    o.uv_ = input.uv_;
    o.position_ = mul(draw.proj_, float4(posVS, 1.0f));
    return o;
}
