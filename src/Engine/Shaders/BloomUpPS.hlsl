#include "Bloom.hlsli"

float4 main(BloomInput i) : SV_Target
{
    const float2 uv = BloomUv(i.ndc_);
    const float2 t = BloomTexel();
    const float2 r = t * g_draw.bloomRadius_;

    float3 c = BloomFetch(uv, t) * 4.0f;
    c += (BloomFetch(uv + float2(-r.x, 0.0f), t) + BloomFetch(uv + float2(r.x, 0.0f), t) +
          BloomFetch(uv + float2(0.0f, -r.y), t) + BloomFetch(uv + float2(0.0f, r.y), t)) *
         2.0f;
    c += BloomFetch(uv + float2(-r.x, -r.y), t) + BloomFetch(uv + float2(r.x, -r.y), t) +
         BloomFetch(uv + float2(-r.x, r.y), t) + BloomFetch(uv + float2(r.x, r.y), t);
    return float4(c / 16.0f, 0.0f);
}
