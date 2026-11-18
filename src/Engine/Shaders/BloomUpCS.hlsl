#include "Bloom.hlsli"
#include "ScreenCompute.hlsli"

static const float BloomScatter = 0.7f;

float4 BloomUp(BloomInput i)
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
    // Blended ONE, ONE_MINUS_SRC_ALPHA in main: alpha is what keeps 1 - scatter of the level below.
    return float4(c / 16.0f * BloomScatter, BloomScatter);
}

[numthreads(ScreenGroupSize, ScreenGroupSize, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 size = ScreenImageSize(g_draw.storageImage_);
    if (any(id.xy >= size))
        return;
    BloomInput i;
    i.svpos_ = float4(float2(id.xy) + 0.5f, 0.0f, 1.0f);
    i.ndc_ = ScreenNdc(id.xy, size);
    const float4 up = BloomUp(i);
    const float4 below = g_storageImages[g_draw.storageImage_][id.xy];
    g_storageImages[g_draw.storageImage_][id.xy] = up + below * (1.0f - up.a);
}
