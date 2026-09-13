#ifndef BATAP_LIGHTING_HLSLI
#define BATAP_LIGHTING_HLSLI

#include "ShaderInterop.h"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::binding(PointLightsBinding, FrameSet)]]
StructuredBuffer<PointLightGPUData> PointLightBuffer;
[[vk::binding(MaterialsBinding, FrameSet)]]
StructuredBuffer<Material> MaterialBuffer;
[[vk::binding(SkyboxBinding, FrameSet)]]
StructuredBuffer<SkyboxGPUData> SkyboxBuffer;

[[vk::binding(SamplerBinding, BindlessSet)]]  SamplerState      g_sampler;
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];

[[vk::push_constant]] DrawPush g_draw;

static const float PI = 3.14159265358979f;

float3 EvalSH9(float3 N)
{
    float4 sh[9] = SkyboxBuffer[0].sh;
    return max(0.0f,
          sh[0].rgb * 0.886227f
        + sh[1].rgb * (1.023327f * N.y)
        + sh[2].rgb * (1.023327f * N.z)
        + sh[3].rgb * (1.023327f * N.x)
        + sh[4].rgb * (0.858086f * N.x * N.y)
        + sh[5].rgb * (0.858086f * N.y * N.z)
        + sh[6].rgb * (0.743125f * N.y * N.y - 0.247708f)
        + sh[7].rgb * (0.858086f * N.x * N.z)
        + sh[8].rgb * (0.429043f * (N.x * N.x - N.z * N.z)));
}

float3 SampleSky(float3 dir, float mipLevel)
{
    SkyboxGPUData sky = SkyboxBuffer[0];
    float3 result;
    if (sky.mode == 0u && sky.bindlessIndex != InvalidGPUIndex)
    {
        float  phi   = atan2(dir.z, dir.x);
        float  theta = asin(clamp(dir.y, -1.0f, 1.0f));
        float2 uv    = float2((phi + PI) / (2.0f * PI), 0.5f - theta / PI);
        result = g_textures[sky.bindlessIndex].SampleLevel(g_sampler, uv, mipLevel).rgb;
    }
    else if (sky.mode == 1u)
    {
        result = sky.color1.rgb;
    }
    else
    {
        float  hw  = max(sky.horizonWidth, 0.001f);
        float  t_u = smoothstep(0.0f, hw, dir.y);
        float  t_d = smoothstep(0.0f, hw, -dir.y);
        float3 col = lerp(sky.color2.rgb, sky.color1.rgb, t_u);
        col        = lerp(col, sky.color3.rgb, t_d);
        result = col;
    }
    return result * sky.intensity;
}

// GGX Normal Distribution Function
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d);
}

// Smith-Schlick-GGX Geometry term (single direction)
float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

// Smith combined geometry term
float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick
float3 F_Schlick(float HdotV, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - HdotV), 5.0f);
}

struct Surface
{
    float3 albedo_;
    float  roughness_;
    float  metallic_;
    float  reflectivity_;
    float3 N_;
    float3 posWS_;
};

float3 ShadeSurface(uint shadingModel, Surface s)
{
    if (shadingModel == ShadingUnlit)
        return s.albedo_;

    CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];

    // F0 : diélectrique = 0.04, métal = albedo
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), s.albedo_, s.metallic_);

    float3 V = normalize(cam.pos_ - s.posWS_);
    float  NdotV = saturate(dot(s.N_, V));

    // IBL diffuse (SH L2) + spéculaire (env sampling). The buffer keeps the last
    // sky's values after it is removed, so the count is what says it is gone.
    float3 color = float3(0.0f, 0.0f, 0.0f);
    if (g_draw.skyboxCount_ != 0)
    {
        float3 F_ibl = F_Schlick(NdotV, F0);
        float3 kD    = (1.0f - F_ibl) * (1.0f - s.metallic_);

        float3 R        = reflect(-V, s.N_);
        float  mipLevel = s.roughness_ * float(max(SkyboxBuffer[0].mipCount, 1u) - 1u);
        float3 specIBL  = F_ibl * SampleSky(R, mipLevel) * s.reflectivity_;

        color = kD * EvalSH9(s.N_) * s.albedo_ + specIBL;
    }

    [loop]
    for (uint lightIndex = 0; lightIndex < g_draw.pointLightCount_; ++lightIndex)
    {
        PointLightGPUData light = PointLightBuffer[lightIndex];

        float3 toLight = light.pos_ - s.posWS_;
        float  dist    = length(toLight);

        if (dist > light.radius_ || dist <= 0.0001f)
            continue;

        float3 L = toLight / dist;
        float3 H = normalize(V + L);

        float rangeAtt   = saturate(1.0f - dist / light.radius_);
        float attenuation = pow(rangeAtt, max(light.falloff_, 0.0001f)) * light.intensity_;
        float3 radiance   = light.color_ * attenuation;

        float NdotL = saturate(dot(s.N_, L));
        float NdotH = saturate(dot(s.N_, H));
        float HdotV = saturate(dot(H, V));

        // Cook-Torrance specular
        float  D = D_GGX(NdotH, s.roughness_);
        float  G = G_Smith(NdotV, NdotL, s.roughness_);
        float3 F = F_Schlick(HdotV, F0);

        float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.0001f);

        // Diffuse lambertien : les métaux n'ont pas de diffuse
        float3 kD_light = (1.0f - F) * (1.0f - s.metallic_);
        float3 diffuse = kD_light * s.albedo_ / PI;

        color += (diffuse + specular) * radiance * NdotL;
    }

    return color;
}

#endif
