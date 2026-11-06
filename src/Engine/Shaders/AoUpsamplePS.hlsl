#include "Ao.hlsli"

// Back to full resolution: the four nearest half-resolution texels, weighted
// bilinearly and by how well their surface continues this pixel's. A pixel no
// texel stands for (thinner than the 2x2 block) gets no occlusion.
float main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData frame = FrameBuffer[0];
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const Texture2D<float4> depths = g_textures[frame.depthTexture_];
    const Texture2D<float4> filtered = g_textures[frame.aoFilteredTexture_];
    const int2 pixel = int2(svpos.xy);
    const float depth = depths.Load(int3(pixel, 0)).r;
    if (depth <= 0.0f)
        return 1.0f;

    const AoScreen v = AoScreenOf(depths, cam);
    const float3 P = AoViewPosition(v, svpos.xy, v.znear_ / depth);
    const float3 N = AoViewNormal(pixel);
    uint width;
    uint height;
    filtered.GetDimensions(width, height);
    const int2 last = int2(width, height) - 1;

    const float2 at = svpos.xy * 0.5f - 0.25f;
    const int2 base = int2(floor(at));
    const float2 f = at - float2(base);

    float sum = 0.0f;
    float weight = 0.0f;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const int2 offset = int2(i & 1, i >> 1);
        const int2 q = clamp(base + offset, int2(0, 0), last);
        const int2 qFull = AoFullTexel(q);
        const float d = depths.Load(int3(qFull, 0)).r;
        if (d <= 0.0f)
            continue;
        const float3 Q = AoViewPosition(v, float2(qFull) + 0.5f, v.znear_ / d);
        const float2 bilinear = lerp(1.0f - f, f, float2(offset));
        const float w = bilinear.x * bilinear.y * AoSurfaceWeight(P, N, Q, AoViewNormal(qFull));
        sum += w * filtered.Load(int3(q, 0)).r;
        weight += w;
    }
    const float ao = weight > 1e-3f ? sum / weight : 1.0f;
    return lerp(1.0f, saturate(ao), frame.aoStrength_);
}
