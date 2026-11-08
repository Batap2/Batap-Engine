#include "ReflectPass.hlsli"

static const uint ReflectReuseSamples = 4u;
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

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData frame = FrameBuffer[0];
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const int2 pixel = int2(svpos.xy);
    const float4 raw = g_textures[frame.reflectRawTexture_].Load(int3(pixel, 0));
    if (raw.a == 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    const float4 ray = g_textures[frame.reflectRayTexture_].Load(int3(pixel, 0));
    if (raw.a > 0.0f && ray.w <= 0.0f)
        return float4(raw.rgb, 1.0f);

    const float2 size = ReflectScreenSize();
    ReflectPixel self;
    ReflectLoadPixel(cam, pixel, size, self);
    const float3 N = self.surface_.N_;
    const float3 V = -normalize(self.P_);
    const float alpha = self.surface_.roughness_ * self.surface_.roughness_;
    const float viewDepth = dot(self.P_, cam.fwd_);

    float weight = raw.a > 0.0f ? ReflectReuseWeight(N, V, ray.xyz, alpha, ray.w) : 0.0f;
    float3 sum = raw.rgb * weight;

    const float hitDistance = raw.a > 0.0f ? raw.a : ReflectSkyDistance;
    const float pixelWorld = 2.0f * viewDepth / (abs(cam.proj_[1][1]) * size.y);
    const float footprint = alpha * hitDistance * abs(dot(self.Ngeom_, V));
    const float radius = clamp(footprint / max(pixelWorld, 1e-6f), ReflectReuseMinRadius,
                               ReflectReuseMaxRadius * size.y);
    uint seed = GiHash(uint(pixel.x) + GiHash(uint(pixel.y) + GiHash(frame.frameIndex_ + 7u)));
    const float rotation = GiRand(seed) * 6.28318531f;
    [unroll]
    for (uint i = 0; i < ReflectReuseSamples; ++i)
    {
        const float angle = rotation + float(i) * ReflectGoldenAngle;
        const float r = radius * sqrt((float(i) + 0.5f) / float(ReflectReuseSamples));
        const int2 q = pixel + int2(round(float2(cos(angle), sin(angle)) * r));
        if (all(q == pixel) || any(q < 0) || any(q >= int2(size)))
            continue;
        const float4 otherRaw = g_textures[frame.reflectRawTexture_].Load(int3(q, 0));
        if (otherRaw.a <= 0.0f)
            continue;
        const float4 otherRay = g_textures[frame.reflectRayTexture_].Load(int3(q, 0));
        if (otherRay.w <= 0.0f)
            continue;
        const float3 otherP =
            ReflectPosition(cam, q, size, g_textures[frame.depthTexture_].Load(int3(q, 0)).r);
        if (abs(dot(otherP - self.P_, self.Ngeom_)) > ReflectPlaneTolerance * viewDepth)
            continue;
        const float3 L = otherRaw.a >= ReflectSkyDistance * 0.5f
                             ? otherRay.xyz
                             : normalize(otherP + otherRay.xyz * otherRaw.a - self.P_);
        const float w = ReflectReuseWeight(N, V, L, alpha, otherRay.w);
        sum += otherRaw.rgb * w;
        weight += w;
    }
    return weight > 0.0f ? float4(sum / weight, 1.0f) : float4(0.0f, 0.0f, 0.0f, 0.0f);
}
