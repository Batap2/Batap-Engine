#include "MeshInstances.hlsli"
#include "Motion.hlsli"
#include "ReflectPass.hlsli"
#include "Scatter.hlsli"
#include "Terrain.hlsli"

static const float ReflectHistoryMax = 32.0f;
static const float ReflectHistoryRamp = 0.25f;
static const float ReflectHistoryNormal = 0.9f;
static const float ReflectHistoryPlane = 0.01f;
static const float ReflectHistoryNearFloor = 0.05f;
static const float ReflectHistoryNearTolerance = 0.15f;
static const float ReflectHistoryNearRamp = 0.25f;
static const float ReflectHistoryNearSpread = 1.0f;
static const float ReflectNearnessMax = 1000.0f;

struct TemporalOutput
{
    float4 color_ : SV_Target0;
    uint4 surface_ : SV_Target1;
};

void ReflectPlacement(ReflectSurface s, CameraGPUData cam, out float4x4 placed,
                      out float4x4 prevPlaced)
{
    if (s.kind_ == ReflectKindTerrain)
    {
        placed = TerrainWorldBuffer[s.id_].world_;
        prevPlaced = TerrainWorldBuffer[s.id_].prevWorld_;
    }
    else if (s.kind_ == ReflectKindScatter)
    {
        placed = ScatterBatchBuffer[s.id_].placed_.world_;
        prevPlaced = ScatterBatchBuffer[s.id_].placed_.prevWorld_;
    }
    else
    {
        const StaticMeshGPUData inst = StaticMeshInstancebuffer[s.id_];
        placed = RelativeTo(inst.world_, cam.pos_);
        prevPlaced = RelativeTo(inst.prevWorld_, cam.prevPos_);
    }
}

// The previous camera's rotation rows come out of prevViewProj_ = proj x
// rotation, with the projection of Camera_C::make_proj.
float3 ReflectPreviousPosition(CameraGPUData cam, float2 ndc, float viewDepth)
{
    const float p00 = cam.proj_[0][0];
    const float p11 = cam.proj_[1][1];
    return (ndc.x * viewDepth / (p00 * p00)) * cam.prevViewProj_[0].xyz +
           (ndc.y * viewDepth / (p11 * p11)) * cam.prevViewProj_[1].xyz +
           viewDepth * cam.prevViewProj_[3].xyz;
}

uint ReflectPackNearness(float mean, float meanSquare)
{
    const float spread = sqrt(max(meanSquare - mean * mean, 0.0f));
    return f32tof16(mean) | (f32tof16(spread) << 16);
}

float2 ReflectUnpackNearness(uint v)
{
    const float mean = f16tof32(v & 0xFFFFu);
    const float spread = f16tof32(v >> 16);
    return float2(mean, spread * spread + mean * mean);
}

float ReflectHistoryLength(float roughness)
{
    return 1.0f + (ReflectHistoryMax - 1.0f) *
                      saturate((roughness - ReflectMirrorRoughness) / ReflectHistoryRamp);
}

TemporalOutput main(float4 svpos : SV_POSITION)
{
    const FrameGPUData frame = FrameBuffer[0];
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const int2 pixel = int2(svpos.xy);

    TemporalOutput o;
    o.color_ = float4(0.0f, 0.0f, 0.0f, 0.0f);
    o.surface_ = uint4(0u, 0u, 0u, 0u);
    const float2 size = ReflectScreenSize();
    ReflectPixel self;
    if (!ReflectLoadPixel(cam, pixel, size, self))
        return o;
    const uint2 packed = g_uintTextures[frame.reflectSurfaceTexture_].Load(int3(pixel, 0)).xy;
    const float viewDepth = dot(self.P_, cam.fwd_);
    o.surface_ = uint4(packed, asuint(viewDepth), 0u);

    const float4 current = g_textures[frame.reflectResolvedTexture_].Load(int3(pixel, 0));
    if (current.a <= 0.0f)
        return o;
    const float nearness = min(1.0f / current.a, ReflectNearnessMax);
    float localNearness = 0.0f;
    float localSquare = 0.0f;
    float localCount = 0.0f;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const float hit =
                g_textures[frame.reflectResolvedTexture_].Load(int3(pixel + int2(dx, dy), 0)).a;
            if (hit > 0.0f)
            {
                const float n = min(1.0f / hit, ReflectNearnessMax);
                localNearness += n;
                localSquare += n * n;
                localCount += 1.0f;
            }
        }
    }
    localNearness /= localCount;
    const float localSpread =
        sqrt(max(localSquare / localCount - localNearness * localNearness, 0.0f));

    const float maxLength = ReflectHistoryLength(self.surface_.roughness_);
    float4 history = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float2 historyNearness = float2(nearness, nearness * nearness);
    float confidence = 0.0f;
    if (frame.reflectHistoryTexture_ != InvalidGPUIndex && maxLength > 1.0f)
    {
        float4x4 placed;
        float4x4 prevPlaced;
        ReflectPlacement(self.surface_, cam, placed, prevPlaced);
        const float3 virtualPoint = self.P_ + normalize(self.P_) * current.a;
        const float3 prevVirtual = PreviousPosition(virtualPoint, placed, prevPlaced);
        const float3 prevP = PreviousPosition(self.P_, placed, prevPlaced);
        const float4 clip = mul(cam.prevViewProj_, float4(prevVirtual, 1.0f));
        if (clip.w > 0.0f)
        {
            const float2 ndc = clip.xy / clip.w;
            const float2 at = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * size - 0.5f;
            const int2 base = int2(floor(at));
            const float2 f = at - float2(base);
            const uint identity = packed.y & 0x3FFFFFu;
            float weight = 0.0f;
            float2 stats = float2(0.0f, 0.0f);
            [unroll]
            for (uint k = 0; k < 4u; ++k)
            {
                const int2 offset = int2(k & 1u, k >> 1);
                const int2 q = base + offset;
                if (any(q < 0) || any(q >= int2(size)))
                    continue;
                const uint4 previous =
                    g_uintTextures[frame.reflectHistorySurfaceTexture_].Load(int3(q, 0));
                const float previousDepth = asfloat(previous.z);
                if (previousDepth <= 0.0f || (previous.y & 0x3FFFFFu) != identity ||
                    dot(ReflectOctUnpack(previous.x), self.surface_.N_) < ReflectHistoryNormal)
                    continue;
                const float3 Q = ReflectPreviousPosition(
                    cam, ReflectPixelNdc(float2(q) + 0.5f, size), previousDepth);
                if (abs(dot(Q - prevP, self.surface_.N_)) > ReflectHistoryPlane * viewDepth)
                    continue;
                const float2 bilinear = lerp(1.0f - f, f, float2(offset));
                const float w = bilinear.x * bilinear.y;
                history += w * g_textures[frame.reflectHistoryTexture_].Load(int3(q, 0));
                stats += w * ReflectUnpackNearness(previous.w);
                weight += w;
            }
            if (weight > 1e-3f)
            {
                history /= weight;
                historyNearness = stats / weight;
                const float spread = max(sqrt(max(historyNearness.y - historyNearness.x *
                                                                          historyNearness.x,
                                                  0.0f)),
                                         localSpread);
                const float change =
                    max(abs(historyNearness.x - localNearness) -
                            ReflectHistoryNearSpread * spread,
                        0.0f) /
                    (historyNearness.x + localNearness + ReflectHistoryNearFloor);
                confidence = saturate(1.0f - (change - ReflectHistoryNearTolerance) /
                                                 ReflectHistoryNearRamp);
            }
            else
            {
                history = float4(0.0f, 0.0f, 0.0f, 0.0f);
                confidence = 0.0f;
            }
        }
    }

    const float rescale =
        frame.prevExposure_ > 0.0f ? frame.exposure_ / frame.prevExposure_ : 1.0f;
    const float historyLength = min(history.a * confidence + 1.0f, maxLength);
    o.color_ = float4(lerp(history.rgb * rescale, current.rgb, 1.0f / historyLength),
                      historyLength);
    const float2 nearnessStats =
        lerp(historyNearness, float2(nearness, nearness * nearness), 1.0f / historyLength);
    o.surface_.w = ReflectPackNearness(nearnessStats.x, nearnessStats.y);
    return o;
}
