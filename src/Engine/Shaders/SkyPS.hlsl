// Modèle de binding Vulkan (docs/vulkan.md §10).
// Les paramètres du ciel viennent du SkyboxBuffer partagé avec le PixelShader
// (IBL) : une seule source de vérité.
#include "ShaderInterop.h"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraBuffer;
[[vk::binding(SkyboxBinding, FrameSet)]]
StructuredBuffer<SkyboxGPUData> SkyboxBuffer;

[[vk::binding(SamplerBinding, BindlessSet)]]  SamplerState      g_sampler;
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];

[[vk::push_constant]] DrawPush g_draw;

static const float PI = 3.14159265358979f;

float4 main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0) : SV_Target
{
    CameraGPUData cam = CameraBuffer[g_draw.cameraIndex_];
    SkyboxGPUData sky = SkyboxBuffer[0];

    float3 dir = normalize(
        (ndc.x / cam.proj_[0][0]) * cam.right_ +
        (ndc.y / cam.proj_[1][1]) * cam.up_    +
        cam.fwd_);

    float3 color;

    if (sky.mode == 0u && sky.bindlessIndex != InvalidGPUIndex)  // HDRI
    {
        float phi   = atan2(dir.z, dir.x);
        float theta = asin(clamp(dir.y, -1.0f, 1.0f));
        float2 uv   = float2((phi + PI) / (2.0f * PI), 0.5f - theta / PI);
        color = g_textures[sky.bindlessIndex].Sample(g_sampler, uv).rgb;
    }
    else if (sky.mode == 1u)  // FlatColor
    {
        color = sky.color1.rgb;
    }
    else  // Gradient : ciel → horizon → bas
    {
        float safeWidth  = max(sky.horizonWidth, 0.01f);
        float upBlend    = smoothstep(0.0f, safeWidth, dir.y);
        float downBlend  = smoothstep(0.0f, safeWidth, -dir.y);
        color = lerp(sky.color2.rgb, sky.color1.rgb,  upBlend);
        color = lerp(color,          sky.color3.rgb,  downBlend);
    }

    return float4(color, 1.0f);
}
