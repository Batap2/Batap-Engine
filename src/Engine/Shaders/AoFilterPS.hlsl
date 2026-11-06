#include "Ao.hlsli"

static const float AoFilterWeights[5] = {1.0f, 4.0f, 6.0f, 4.0f, 1.0f};
static const float AoFilterNormalPower = 8.0f;
static const float AoFilterPlaneTolerance = 0.02f;

// Binomial 5x5, cut where a texel leaves the centre's tangent plane or turns
// its normal away. Linear: AoPS writes an unbiased estimate, not a ratio.
float main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData frame = FrameBuffer[0];
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const Texture2D<float4> depths = g_textures[frame.depthTexture_];
    const Texture2D<float4> raw = g_textures[frame.aoRawTexture_];
    const int2 centre = int2(svpos.xy);
    const float depth = depths.Load(int3(centre, 0)).r;
    if (depth <= 0.0f)
        return 1.0f;

    const AoScreen v = AoScreenOf(depths, cam);
    const float L = v.znear_ / depth;
    const float3 P = AoViewPosition(v, svpos.xy, L);
    const float3 N = AoViewNormal(centre);
    const float invTolerance = 1.0f / (AoFilterPlaneTolerance * L);
    const int2 last = int2(v.size_) - 1;

    float sum = 0.0f;
    float weight = 0.0f;
    [unroll]
    for (int dy = -2; dy <= 2; ++dy)
    {
        [unroll]
        for (int dx = -2; dx <= 2; ++dx)
        {
            const int2 q = clamp(centre + int2(dx, dy), int2(0, 0), last);
            const float d = depths.Load(int3(q, 0)).r;
            if (d <= 0.0f)
                continue;
            const float3 Q = AoViewPosition(v, float2(q) + 0.5f, v.znear_ / d);
            const float w = AoFilterWeights[dx + 2] * AoFilterWeights[dy + 2] *
                            saturate(1.0f - abs(dot(Q - P, N)) * invTolerance) *
                            pow(saturate(dot(N, AoViewNormal(q))), AoFilterNormalPower);
            sum += w * raw.Load(int3(q, 0)).r;
            weight += w;
        }
    }
    const float ao = weight > 0.0f ? sum / weight : raw.Load(int3(centre, 0)).r;
    return lerp(1.0f, saturate(ao), frame.aoStrength_);
}
