#include "ReflectPass.hlsli"

static const uint ReflectFilterTaps = 8u;
static const float ReflectFilterMaxRadius = 0.02f;
static const float ReflectFilterPlane = 0.01f;
static const float ReflectFilterNormalPower = 32.0f;
static const float ReflectFilterRoughness = 0.1f;
static const float ReflectFilterGoldenAngle = 2.39996323f;

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData frame = FrameBuffer[0];
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const int2 pixel = int2(svpos.xy);
    const float4 centre = g_textures[frame.reflectTemporalTexture_].Load(int3(pixel, 0));
    if (centre.a <= 0.0f)
        return centre;
    const float2 size = ReflectScreenSize();
    ReflectPixel self;
    if (!ReflectLoadPixel(cam, pixel, size, self) ||
        self.surface_.roughness_ <= ReflectMirrorRoughness)
        return centre;

    const float hit = g_textures[frame.reflectResolvedTexture_].Load(int3(pixel, 0)).a;
    const float hitDistance = hit > 0.0f ? hit : ReflectSkyDistance;
    const float3 V = -normalize(self.P_);
    const float viewDepth = dot(self.P_, cam.fwd_);
    const float alpha = self.surface_.roughness_ * self.surface_.roughness_;
    const float pixelWorld = 2.0f * viewDepth / (abs(cam.proj_[1][1]) * size.y);
    const float footprint = alpha * hitDistance * abs(dot(self.Ngeom_, V));
    const float radius = min(footprint / max(pixelWorld, 1e-6f), ReflectFilterMaxRadius * size.y) *
                         rsqrt(max(centre.a, 1.0f));
    if (radius < 1.0f)
        return centre;

    uint seed = GiHash(uint(pixel.x) + GiHash(uint(pixel.y) + 13u));
    const float rotation =
        frac(GiRand(seed) + float(frame.frameIndex_ % ReflectSequencePeriod) * ReflectGoldenRatio) *
        6.28318531f;
    float3 sum = centre.rgb;
    float weight = 1.0f;
    [unroll]
    for (uint i = 0; i < ReflectFilterTaps; ++i)
    {
        const float angle = rotation + float(i) * ReflectFilterGoldenAngle;
        const float r = radius * sqrt((float(i) + 0.5f) / float(ReflectFilterTaps));
        const int2 q = pixel + int2(round(float2(cos(angle), sin(angle)) * r));
        if (all(q == pixel) || any(q < 0) || any(q >= int2(size)))
            continue;
        const float4 other = g_textures[frame.reflectTemporalTexture_].Load(int3(q, 0));
        if (other.a <= 0.0f)
            continue;
        const ReflectSurface s =
            ReflectSurfaceDecode(g_uintTextures[frame.reflectSurfaceTexture_].Load(int3(q, 0)).xy);
        const float3 otherP =
            ReflectPosition(cam, q, size, g_textures[frame.depthTexture_].Load(int3(q, 0)).r);
        if (abs(dot(otherP - self.P_, self.Ngeom_)) > ReflectFilterPlane * viewDepth)
            continue;
        const float w = pow(saturate(dot(s.N_, self.surface_.N_)), ReflectFilterNormalPower) *
                        saturate(1.0f - abs(s.roughness_ - self.surface_.roughness_) /
                                            ReflectFilterRoughness);
        sum += other.rgb * w;
        weight += w;
    }
    return float4(sum / weight, centre.a);
}
