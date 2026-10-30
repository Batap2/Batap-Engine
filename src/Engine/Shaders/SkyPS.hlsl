// Modèle de binding Vulkan (docs/vulkan.md §10).
// Les paramètres du ciel viennent du SkyboxBuffer partagé avec le PixelShader
// (IBL) : une seule source de vérité.
#include "Sky.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraBuffer;
[[vk::binding(SkyboxBinding, FrameSet)]]
StructuredBuffer<SkyboxGPUData> SkyboxBuffer;
[[vk::binding(LightsBinding, FrameSet)]]
StructuredBuffer<LightGPUData> LightBuffer;
[[vk::binding(FrameConstantsBinding, FrameSet)]]
StructuredBuffer<FrameGPUData> FrameBuffer;

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
    else
    {
        color = SkyBackground(sky, dir);
    }
    color *= sky.intensity;

    SkySun sun = SkyNoSun();
    for (uint i = 0; i < g_draw.lightCount_ && !sun.valid; ++i)
        sun = SkySunOf(LightBuffer[i], cam.pos_);
    color = SkyComposite(g_draw.atmosphereCount_, cam.pos_, dir, sun, color, true);

    return float4(color * FrameBuffer[0].exposure_, 1.0f);
}
