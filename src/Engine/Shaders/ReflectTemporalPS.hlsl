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
static const float ReflectNearnessMax = 1000.0f;
static const float ReflectVirtualFrom = 0.1f;
static const float ReflectVirtualTo = 0.4f;
static const float ReflectHistoryParallax = 0.5f;
static const float ReflectLobeAngle = 1.3f;

struct TemporalOutput
{
    float4 color_ : SV_Target0;
    uint4 surface_ : SV_Target1;
};

struct ReflectHistory
{
    float4 color_;
    float nearness_;
    float weight_;
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
        const TerrainWorldGPUData scatter = ScatterPlaced(ScatterBatchBuffer[s.id_]);
        placed = scatter.world_;
        prevPlaced = scatter.prevWorld_;
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

float ReflectHistoryLength(float roughness)
{
    return 1.0f + (ReflectHistoryMax - 1.0f) *
                      saturate((roughness - ReflectMirrorRoughness) / ReflectHistoryRamp);
}

ReflectHistory ReflectFetchHistory(CameraGPUData cam, float2 size, float3 prevPoint, float3 prevP,
                                   float3 N, uint identity, float viewDepth)
{
    const FrameGPUData frame = FrameBuffer[0];
    ReflectHistory h;
    h.color_ = float4(0.0f, 0.0f, 0.0f, 0.0f);
    h.nearness_ = 0.0f;
    h.weight_ = 0.0f;
    const float4 clip = mul(cam.prevViewProj_, float4(prevPoint, 1.0f));
    if (clip.w <= 0.0f)
        return h;
    const float2 ndc = clip.xy / clip.w;
    const float2 at = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * size - 0.5f;
    const int2 base = int2(floor(at));
    const float2 f = at - float2(base);
    [unroll]
    for (uint k = 0; k < 4u; ++k)
    {
        const int2 offset = int2(k & 1u, k >> 1);
        const int2 q = base + offset;
        if (any(q < 0) || any(q >= int2(size)))
            continue;
        const uint4 previous = g_uintTextures[frame.reflectHistorySurfaceTexture_].Load(int3(q, 0));
        const float previousDepth = asfloat(previous.z);
        if (previousDepth <= 0.0f || (previous.y & 0x3FFFFFu) != identity ||
            dot(ReflectOctUnpack(previous.x), N) < ReflectHistoryNormal)
            continue;
        const float3 Q =
            ReflectPreviousPosition(cam, ReflectPixelNdc(float2(q) + 0.5f, size), previousDepth);
        if (abs(dot(Q - prevP, N)) > ReflectHistoryPlane * viewDepth)
            continue;
        const float2 bilinear = lerp(1.0f - f, f, float2(offset));
        const float w = bilinear.x * bilinear.y;
        h.color_ += w * g_textures[frame.reflectHistoryTexture_].Load(int3(q, 0));
        h.nearness_ += w * asfloat(previous.w);
        h.weight_ += w;
    }
    if (h.weight_ > 1e-3f)
    {
        h.color_ /= h.weight_;
        h.nearness_ /= h.weight_;
    }
    else
        h.weight_ = 0.0f;
    return h;
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
    float localCount = 0.0f;
    float localLow = ReflectNearnessMax;
    float localHigh = 0.0f;
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
                localCount += 1.0f;
                localLow = min(localLow, n);
                localHigh = max(localHigh, n);
            }
        }
    }
    localNearness /= localCount;

    float maxLength = ReflectHistoryLength(self.surface_.roughness_);
    float4 history = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float historyNearness = nearness;
    float confidence = 0.0f;
    if (frame.reflectHistoryTexture_ != InvalidGPUIndex && maxLength > 1.0f)
    {
        float4x4 placed;
        float4x4 prevPlaced;
        ReflectPlacement(self.surface_, cam, placed, prevPlaced);
        const float3 prevP = PreviousPosition(self.P_, placed, prevPlaced);
        const float parallax = length(cross(normalize(self.P_), normalize(prevP)));
        const float lobe =
            ReflectLobeAngle * self.surface_.roughness_ * self.surface_.roughness_;
        maxLength = clamp(ReflectHistoryParallax * lobe / max(parallax, 1e-6f), 1.0f, maxLength);
        const uint identity = packed.y & 0x3FFFFFu;
        const float virtualAmount =
            1.0f - smoothstep(ReflectVirtualFrom, ReflectVirtualTo, self.surface_.roughness_);
        const ReflectHistory surface =
            ReflectFetchHistory(cam, size, prevP, prevP, self.surface_.N_, identity, viewDepth);
        ReflectHistory chosen = surface;
        float trust = 1.0f - virtualAmount;
        if (virtualAmount > 0.0f)
        {
            const float3 virtualPoint = self.P_ + normalize(self.P_) * current.a;
            const ReflectHistory reflected = ReflectFetchHistory(
                cam, size, PreviousPosition(virtualPoint, placed, prevPlaced), prevP,
                self.surface_.N_, identity, viewDepth);
            if (reflected.weight_ > 0.0f)
            {
                const float blend = surface.weight_ > 0.0f ? virtualAmount : 1.0f;
                chosen.color_ = lerp(surface.color_, reflected.color_, blend);
                chosen.nearness_ = lerp(surface.nearness_, reflected.nearness_, blend);
                chosen.weight_ = reflected.weight_;
                trust = 1.0f;
            }
        }
        if (chosen.weight_ > 0.0f)
        {
            history = chosen.color_;
            historyNearness = chosen.nearness_;
            const float change =
                max(max(localLow - historyNearness, historyNearness - localHigh), 0.0f) /
                (historyNearness + localNearness + ReflectHistoryNearFloor);
            confidence = trust * saturate(1.0f - (change - ReflectHistoryNearTolerance) /
                                                     ReflectHistoryNearRamp);
        }
    }

    const float rescale =
        frame.prevExposure_ > 0.0f ? frame.exposure_ / frame.prevExposure_ : 1.0f;
    const float historyLength = min(history.a * confidence + 1.0f, maxLength);
    o.color_ = float4(lerp(history.rgb * rescale, current.rgb, 1.0f / historyLength),
                      historyLength);
    o.surface_.w = asuint(lerp(historyNearness, nearness, 1.0f / historyLength));
    return o;
}
