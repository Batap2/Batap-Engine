#include "ReflectPass.hlsli"
#include "ScreenCompute.hlsli"

static const uint ReflectReuseSamples = 4u;
static const uint ReflectReuseRoughSamples = 8u;
static const float ReflectReuseRough = 0.25f;
static const float ReflectReuseMaxRadius = 0.02f;
static const float ReflectReuseMinRadius = 3.0f;
static const float ReflectPlaneTolerance = 0.02f;
static const float ReflectGoldenAngle = 2.39996323f;

float ReflectReuseWeight(float3 N, float3 V, float3 L, float alpha, float pdf)
{
    const float NdotL = dot(N, L);
    if (NdotL <= 0.0f || pdf <= 0.0f)
        return 0.0f;
    const float NdotV = saturate(dot(N, V));
    const float NdotH = saturate(dot(N, normalize(V + L)));
    return D_GGX(NdotH, alpha) * V_SmithGGXCorrelated(NdotV, NdotL, alpha) * NdotL / pdf;
}

float4 ReflectResolve(int2 pixel, int2 size)
{
    const FrameGPUData frame = FrameBuffer[0];
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const uint shift = frame.reflectTraceShift_;
    const int2 offset = ReflectTraceOffset(frame.frameIndex_);
    const int2 home = pixel >> shift;
    const int2 homePixel = ReflectTracedPixel(home, shift, offset, size);
    const float4 raw = g_textures[frame.reflectRawTexture_].Load(int3(home, 0));
    if (shift == 0u && raw.a == 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 ray = g_textures[frame.reflectRayTexture_].Load(int3(home, 0));
    bool hasOwn = raw.a > 0.0f && all(homePixel == pixel);
    if (hasOwn && ray.w <= 0.0f)
        return raw;

    ReflectPixel self;
    if (!ReflectLoadPixel(cam, pixel, float2(size), self) ||
        self.surface_.kind_ == ReflectKindNone || ReflectWeight(self.surface_.roughness_) <= 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 own = raw;
    if (any(homePixel != pixel) && self.surface_.roughness_ <= ReflectOwnRayRoughness)
    {
        own = g_textures[frame.reflectOwnRawTexture_].Load(int3(pixel, 0));
        ray = g_textures[frame.reflectOwnRayTexture_].Load(int3(pixel, 0));
        hasOwn = own.a > 0.0f;
        if (hasOwn && ray.w <= 0.0f)
            return own;
    }
    if (self.surface_.roughness_ <= ReflectMirrorRoughness)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    const float3 V = -normalize(self.P_);
    const float viewDepth = dot(self.P_, cam.fwd_);
    const float3 N = self.surface_.N_;
    const float alpha = self.surface_.roughness_ * self.surface_.roughness_;
    float weight = hasOwn ? ReflectReuseWeight(N, V, ray.xyz, alpha, ray.w) : 0.0f;
    float3 sum = own.rgb * weight;
    float nearness = hasOwn ? weight / own.a : 0.0f;

    const float hitDistance = hasOwn ? own.a : raw.a > 0.0f ? raw.a : ReflectSkyDistance;
    const float pixelWorld = 2.0f * viewDepth / (abs(cam.proj_[1][1]) * float(size.y));
    const float footprint = alpha * hitDistance * abs(dot(self.Ngeom_, V));
    const float radius = clamp(footprint / max(pixelWorld, 1e-6f),
                               ReflectReuseMinRadius * float(1u << shift),
                               ReflectReuseMaxRadius * float(size.y));
    uint seed = GiHash(uint(pixel.x) + GiHash(uint(pixel.y) + 7u));
    const float rotation =
        frac(GiRand(seed) + float(frame.frameIndex_ % ReflectSequencePeriod) * ReflectGoldenRatio) *
        6.28318531f;
    const uint count = self.surface_.roughness_ >= ReflectReuseRough ? ReflectReuseRoughSamples
                                                                     : ReflectReuseSamples;
    [loop]
    for (uint i = 0; i <= count; ++i)
    {
        int2 texel = home;
        if (i > 0u)
        {
            const float angle = rotation + float(i - 1u) * ReflectGoldenAngle;
            const float r = radius * sqrt((float(i) - 0.5f) / float(count));
            const int2 q = pixel + int2(round(float2(cos(angle), sin(angle)) * r));
            if (any(q < 0) || any(q >= size))
                continue;
            texel = q >> shift;
            if (all(texel == home))
                continue;
        }
        else if (all(homePixel == pixel))
            continue;
        const float4 otherRaw = g_textures[frame.reflectRawTexture_].Load(int3(texel, 0));
        if (otherRaw.a <= 0.0f)
            continue;
        const float4 otherRay = g_textures[frame.reflectRayTexture_].Load(int3(texel, 0));
        if (otherRay.w <= 0.0f)
            continue;
        const int2 q = ReflectTracedPixel(texel, shift, offset, size);
        const float3 otherP =
            ReflectPosition(cam, q, float2(size), g_textures[frame.depthTexture_].Load(int3(q, 0)).r);
        if (abs(dot(otherP - self.P_, self.Ngeom_)) > ReflectPlaneTolerance * viewDepth)
            continue;
        const float3 L = otherRaw.a >= ReflectSkyDistance * 0.5f
                             ? otherRay.xyz
                             : normalize(otherP + otherRay.xyz * otherRaw.a - self.P_);
        const float w = ReflectReuseWeight(N, V, L, alpha, otherRay.w);
        sum += otherRaw.rgb * w;
        weight += w;
        nearness += w / otherRaw.a;
    }
    if (weight <= 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    return float4(sum / weight, clamp(weight / max(nearness, 1e-30f), 1e-3f, ReflectSkyDistance));
}

[numthreads(ScreenGroupSize, ScreenGroupSize, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 size = ScreenImageSize(g_draw.storageImage_);
    if (any(id.xy >= size))
        return;
    g_storageImages[g_draw.storageImage_][id.xy] = ReflectResolve(int2(id.xy), int2(size));
}
