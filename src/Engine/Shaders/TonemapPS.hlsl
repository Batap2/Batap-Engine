#include "ShaderInterop.h"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::binding(FrameConstantsBinding, FrameSet)]] StructuredBuffer<FrameGPUData> FrameBuffer;

float3 SrgbEncode(float3 c)
{
    c = saturate(c);
    return select(c <= 0.0031308f, 12.92f * c, 1.055f * pow(c, 1.0f / 2.4f) - 0.055f);
}

float3 AgxContrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 +
           0.1191f * x - 0.00232f;
}

float3 Agx(float3 linearRgb)
{
    const float3x3 inset = float3x3(0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
                                    0.0784335999999992f, 0.878468636469772f, 0.0784336f,
                                    0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f);
    const float3x3 outset = float3x3(1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f,
                                     -0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f,
                                     -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f);
    const float minEv = -12.47393f;
    const float maxEv = 4.026069f;
    float3 c = mul(linearRgb, inset);
    c = clamp(log2(max(c, 1e-10f)), minEv, maxEv);
    c = AgxContrast((c - minEv) / (maxEv - minEv));
    return saturate(mul(c, outset));
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

float3 AcesHill(float3 c)
{
    const float3x3 input = float3x3(0.59719f, 0.35458f, 0.04823f,
                                    0.07600f, 0.90834f, 0.01566f,
                                    0.02840f, 0.13383f, 0.83777f);
    const float3x3 output = float3x3(1.60475f, -0.53108f, -0.07367f,
                                     -0.10208f, 1.10813f, -0.00605f,
                                     -0.00327f, -0.07276f, 1.07602f);
    c = mul(input, c);
    const float3 a = c * (c + 0.0245786f) - 0.000090537f;
    const float3 b = c * (0.983729f * c + 0.4329510f) + 0.238081f;
    return mul(output, a / b);
}

float3 Uchimura(float3 x)
{
    const float maxBrightness = 1.0f;
    const float contrast = 1.0f;
    const float linearStart = 0.22f;
    const float linearLength = 0.4f;
    const float black = 1.33f;
    const float pedestal = 0.0f;

    const float l0 = (maxBrightness - linearStart) * linearLength / contrast;
    const float s0 = linearStart + l0;
    const float s1 = linearStart + contrast * l0;
    const float cp = -contrast / (maxBrightness - s1);

    const float3 toe = linearStart * pow(x / linearStart, black) + pedestal;
    const float3 shoulder = maxBrightness - (maxBrightness - s1) * exp(cp * (x - s0));
    const float3 middle = linearStart + contrast * (x - linearStart);

    const float3 wToe = 1.0f - smoothstep(0.0f, linearStart, x);
    const float3 wShoulder = step(s0, x);
    return toe * wToe + middle * (1.0f - wToe - wShoulder) + shoulder * wShoulder;
}

float3 Tonemap(float3 c, uint op)
{
    switch (op)
    {
    case TonemapAgx:
        return Agx(c);
    case TonemapPbrNeutral:
        return SrgbEncode(PbrNeutral(c));
    case TonemapAces:
        return SrgbEncode(AcesHill(c));
    case TonemapReinhard:
        return SrgbEncode(c / (1.0f + c));
    case TonemapUchimura:
        return SrgbEncode(Uchimura(c));
    default:
        return SrgbEncode(c);
    }
}

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData frame = FrameBuffer[0];
    const float4 hdr = g_textures[frame.hdrTexture_].Load(int3(svpos.xy, 0));
    return float4(Tonemap(max(hdr.rgb, 0.0f), frame.tonemap_), hdr.a);
}
