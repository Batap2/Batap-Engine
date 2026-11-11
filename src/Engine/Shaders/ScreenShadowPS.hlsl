#include "ShaderInterop.h"

[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];
[[vk::binding(FrameConstantsBinding, FrameSet)]] StructuredBuffer<FrameGPUData> FrameBuffer;
[[vk::binding(CamerasBinding, FrameSet)]] StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::binding(LightsBinding, FrameSet)]] StructuredBuffer<LightGPUData> LightBuffer;
[[vk::push_constant]] DrawPush g_draw;

static const uint ScreenShadowSamples = 60u;
static const uint ScreenShadowHardSamples = 4u;
static const uint ScreenShadowFadeSamples = 8u;
static const float ScreenShadowContrast = 4.0f;
static const float ScreenShadowMinThickness = 0.01f;
static const float ScreenShadowThickness = 0.5f;
static const float ScreenShadowEdge = 0.02f;

float main(float4 svpos : SV_POSITION) : SV_Target
{
    const FrameGPUData frame = FrameBuffer[0];
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const Texture2D<float4> depths = g_textures[frame.depthTexture_];
    uint width;
    uint height;
    depths.GetDimensions(width, height);
    const int2 size = int2(width, height);

    const float startDepth = depths.Load(int3(int2(svpos.xy), 0)).r;
    if (startDepth <= 0.0f)
        return 1.0f;

    const LightGPUData light = LightBuffer[frame.screenShadowLight_];
    const float4 toLight = light.type_ == LightDirectional
                               ? float4(-light.direction_, 0.0f)
                               : float4(light.pos_ - cam.pos_, 1.0f);
    const float4 clip =
        mul(cam.proj_, float4(mul((float3x3)cam.view_, toLight.xyz), toLight.w));

    // Behind the camera the light's image is its antipode, and rays to the
    // light leave it instead of converging on it.
    const bool toward = clip.w > 0.0f;
    const float w = abs(clip.w) < 1e-4f ? (toward ? 1e-4f : -1e-4f) : clip.w;
    const float2 lightPixel = (clip.xy / w * float2(0.5f, -0.5f) + 0.5f) * float2(size);
    const float lightDepth = clip.w == 0.0f ? 0.0f : clip.z / clip.w;

    const float2 fromLight = svpos.xy - lightPixel;
    const float startDistance = max(abs(fromLight.x), abs(fromLight.y));
    if (startDistance < 1.0f)
        return 1.0f;
    const float2 step = (toward ? -fromLight : fromLight) / startDistance;
    const float distanceStep = toward ? -1.0f : 1.0f;
    const float depthPerDistance = (startDepth - lightDepth) / startDistance;
    const float thicknessDepth = ScreenShadowThickness / cam.znear_;

    const bool xMajor = abs(fromLight.x) >= abs(fromLight.y);

    float hard = 1.0f;
    float4 soft = float4(1.0f, 1.0f, 1.0f, 1.0f);
    [unroll]
    for (uint i = 0u; i < ScreenShadowSamples; ++i)
    {
        const float k = float(i + 1u);
        const float distance = startDistance + distanceStep * k;
        const float2 position = svpos.xy + step * k;
        const int2 texel = int2(floor(position));
        const float across = frac(xMajor ? position.y : position.x) - 0.5f;
        const int2 neighbour =
            texel + (xMajor ? int2(0, across > 0.0f ? 1 : -1) : int2(across > 0.0f ? 1 : -1, 0));
        const bool inside = distance > 0.0f && all(texel >= 0) && all(texel < size);
        const bool neighbourInside = all(neighbour >= 0) && all(neighbour < size);
        const float nearDepth = inside ? depths.Load(int3(texel, 0)).r : 0.0f;
        const float farDepth =
            inside && neighbourInside ? depths.Load(int3(neighbour, 0)).r : nearDepth;
        const bool edge = abs(nearDepth - farDepth) > ScreenShadowEdge * nearDepth;
        const float sampleDepth = edge ? nearDepth : lerp(nearDepth, farDepth, abs(across));

        // Reversed Z: 1 - ray / sample is how far the surface stands in
        // front of the ray, relative to the ray's distance.
        const float rayDepth = lightDepth + depthPerDistance * distance;
        const float ahead = 1.0f - rayDepth / max(sampleDepth, 1e-30f);
        const float thickness = max(ScreenShadowMinThickness, thicknessDepth * rayDepth);
        const float window = sampleDepth > 0.0f ? abs(2.0f * ahead / thickness - 1.0f) : 1e10f;

        if (i < ScreenShadowHardSamples)
        {
            hard = min(hard, window);
        }
        else
        {
            const uint fadeStart = ScreenShadowSamples - ScreenShadowFadeSamples;
            const float fade = i >= fadeStart ? float(i + 1u - fadeStart) /
                                                    float(ScreenShadowFadeSamples + 1u) * 0.75f
                                              : 0.0f;
            soft[i & 3u] = min(soft[i & 3u], window + fade);
        }
    }

    soft = saturate(soft * ScreenShadowContrast + (1.0f - ScreenShadowContrast));
    hard = saturate(hard * ScreenShadowContrast + (1.0f - ScreenShadowContrast));
    return min(hard, dot(soft, 0.25f));
}
