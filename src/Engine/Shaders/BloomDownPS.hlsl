#include "Bloom.hlsli"

static const float BloomMax = 8000.0f;

float3 BloomBox(float3 a, float3 b, float3 c, float3 d)
{
    return (a + b + c + d) * 0.25f;
}

float4 main(BloomInput i) : SV_Target
{
    const float2 uv = BloomUv(i.ndc_);
    const float2 t = BloomTexel();

    const float3 a = BloomFetch(uv + t * float2(-2.0f, -2.0f), t);
    const float3 b = BloomFetch(uv + t * float2(0.0f, -2.0f), t);
    const float3 c = BloomFetch(uv + t * float2(2.0f, -2.0f), t);
    const float3 d = BloomFetch(uv + t * float2(-2.0f, 0.0f), t);
    const float3 e = BloomFetch(uv, t);
    const float3 f = BloomFetch(uv + t * float2(2.0f, 0.0f), t);
    const float3 g = BloomFetch(uv + t * float2(-2.0f, 2.0f), t);
    const float3 h = BloomFetch(uv + t * float2(0.0f, 2.0f), t);
    const float3 k = BloomFetch(uv + t * float2(2.0f, 2.0f), t);
    const float3 j = BloomFetch(uv + t * float2(-1.0f, -1.0f), t);
    const float3 l = BloomFetch(uv + t * float2(1.0f, -1.0f), t);
    const float3 m = BloomFetch(uv + t * float2(-1.0f, 1.0f), t);
    const float3 n = BloomFetch(uv + t * float2(1.0f, 1.0f), t);

    float3 boxes[5];
    boxes[0] = BloomBox(j, l, m, n);
    boxes[1] = BloomBox(a, b, d, e);
    boxes[2] = BloomBox(b, c, e, f);
    boxes[3] = BloomBox(d, e, g, h);
    boxes[4] = BloomBox(e, f, h, k);
    const float shares[5] = {0.5f, 0.125f, 0.125f, 0.125f, 0.125f};

    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float total = 0.0f;
    [unroll]
    for (uint box = 0; box < 5u; ++box)
    {
        float weight = shares[box];
        if (g_draw.bloomKaris_ != 0u)
            weight /= 1.0f + dot(boxes[box], float3(0.2126f, 0.7152f, 0.0722f));
        sum += boxes[box] * weight;
        total += weight;
    }
    // Every level is summed on the way up: capped so the sum stays a finite half float.
    return float4(min(sum / total, BloomMax), 1.0f);
}
