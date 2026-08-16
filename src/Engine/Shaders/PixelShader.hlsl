// Modèle de binding Vulkan (docs/vulkan.md §10) :
//   set 0 = bindless global (sampler s0, textures t4[])
//   set 1 = données de frame (storage buffers)
//   push constants = indices du draw courant
// Changement vs DX12 : une seule sortie couleur — le normalRT (SV_Target1)
// était écrit mais jamais lu par aucune passe.

struct CameraData
{
    float4x4 view_;
    float4x4 proj_;
    float3 pos_;   float znear_;
    float3 right_; float zfar_;
    float3 up_;    float fov_;
    float3 fwd_;   float pad_;
};

[[vk::binding(0, 1)]] StructuredBuffer<CameraData> CameraInstancebuffer : register(t0);

struct InstanceData
{
    float4x4 world_;              // 64 bytes
    uint     materialIndices_[8]; // 32 bytes
};
[[vk::binding(1, 1)]] StructuredBuffer<InstanceData> StaticMeshInstancebuffer : register(t1);

struct PointLight
{
    float3 pos_;
    float intensity_;
    float3 color_;
    float radius_;
    float falloff_;
    bool castShadows_;
};
[[vk::binding(2, 1)]] StructuredBuffer<PointLight> PointLightBuffer : register(t2);

struct MaterialData
{
    float4 albedo;
    float  roughness;
    float  metallic;
    float  reflectivity;      // 0 = pas de reflet env, 1 = reflet complet
    uint   albedoTexIdx;      // 0xFFFFFFFF = no texture
    uint   normalTexIdx;
    uint   roughnessTexIdx;
    uint   metallicTexIdx;
    uint   pad_;
};
[[vk::binding(3, 1)]] StructuredBuffer<MaterialData> MaterialBuffer : register(t3);

struct DrawPush
{
    uint cameraIndex_;
    uint instanceIndex_;
    uint submeshIndex_;
    uint pointLightCount_;
};
[[vk::push_constant]] DrawPush g_draw;

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION; // clip space
    float3 posWS_    : TEXCOORD0;   // world position
    float3 nrmWS_    : TEXCOORD1;   // world normal
    float2 uv_       : TEXCOORD2;
    float4 tanWS_    : TEXCOORD3;   // xyz = world tangent, w = handedness
};

struct SkyboxGPUData
{
    float4 sh[9];
    uint   mode;
    uint   heapIdx;
    uint   mipCount;
    float  intensity;
    float4 color1;
    float4 color2;
    float4 color3;
    float  horizonWidth;
    float3 pad_;
};
[[vk::binding(4, 1)]] StructuredBuffer<SkyboxGPUData> SkyboxBuffer : register(t0, space1);

[[vk::binding(0, 0)]] SamplerState      g_sampler    : register(s0);
[[vk::binding(1, 0)]] Texture2D<float4> g_textures[] : register(t4);

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
    if (sky.mode == 0u && sky.heapIdx != 0xFFFFFFFFu)
    {
        float  phi   = atan2(dir.z, dir.x);
        float  theta = asin(clamp(dir.y, -1.0f, 1.0f));
        float2 uv    = float2((phi + PI) / (2.0f * PI), 0.5f - theta / PI);
        result = g_textures[sky.heapIdx].SampleLevel(g_sampler, uv, mipLevel).rgb;
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

float4 main(VS_OUTPUT i) : SV_Target
{
    CameraData cam = CameraInstancebuffer[g_draw.cameraIndex_];

    // --------- matériau ----------
    // Un draw par submesh, l'index arrive en push constant : SV_PrimitiveID
    // déclarerait la capability Geometry, absente sur Metal/MoltenVK.
    InstanceData inst = StaticMeshInstancebuffer[g_draw.instanceIndex_];

    // matIdx 0xFFFFFFFF → slot 0 = default material (created at engine init)
    uint matIdx    = inst.materialIndices_[g_draw.submeshIndex_];
    uint safeIdx   = (matIdx != 0xFFFFFFFFu) ? matIdx : 0u;
    MaterialData mat = MaterialBuffer[safeIdx];

    // Texture channels always valid: unassigned → white/flat-normal texture (neutral multiplier)
    float3 albedo    = mat.albedo.rgb    * g_textures[mat.albedoTexIdx].Sample(g_sampler, i.uv_).rgb;
    float  roughness = clamp(mat.roughness * g_textures[mat.roughnessTexIdx].Sample(g_sampler, i.uv_).r, 0.05f, 1.0f);
    float  metallic  = saturate(mat.metallic * g_textures[mat.metallicTexIdx].Sample(g_sampler, i.uv_).r);

    // F0 : diélectrique = 0.04, métal = albedo
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float3 Ngeom = normalize(i.nrmWS_);
    float3 N;
    if (mat.normalTexIdx != 0xFFFFFFFFu)
    {
        float3 normalSample = g_textures[mat.normalTexIdx].Sample(g_sampler, i.uv_).rgb;
        normalSample = normalSample * 2.0f - 1.0f;
        normalSample.z = sqrt(saturate(1.0f - dot(normalSample.xy, normalSample.xy)));

        float3 T = normalize(i.tanWS_.xyz);
        T = normalize(T - dot(T, Ngeom) * Ngeom);
        float3 B = cross(Ngeom, T) * i.tanWS_.w;
        N = normalize(normalSample.x * T + normalSample.y * B + normalSample.z * Ngeom);
    }
    else
    {
        N = Ngeom;
    }
    float3 V = normalize(cam.pos_ - i.posWS_);
    float  NdotV = saturate(dot(N, V));

    // IBL diffuse (SH L2) + spéculaire (env sampling)
    float3 F_ibl = F_Schlick(NdotV, F0);
    float3 kD    = (1.0f - F_ibl) * (1.0f - metallic);

    float3 R        = reflect(-V, N);
    float  mipLevel = roughness * float(max(SkyboxBuffer[0].mipCount, 1u) - 1u);
    float3 specIBL  = F_ibl * SampleSky(R, mipLevel) * mat.reflectivity;

    float3 color = kD * EvalSH9(N) * albedo + specIBL;

    [loop]
    for (uint lightIndex = 0; lightIndex < g_draw.pointLightCount_; ++lightIndex)
    {
        PointLight light = PointLightBuffer[lightIndex];

        float3 toLight = light.pos_ - i.posWS_;
        float  dist    = length(toLight);

        if (dist > light.radius_ || dist <= 0.0001f)
            continue;

        float3 L = toLight / dist;
        float3 H = normalize(V + L);

        float rangeAtt   = saturate(1.0f - dist / light.radius_);
        float attenuation = pow(rangeAtt, max(light.falloff_, 0.0001f)) * light.intensity_;
        float3 radiance   = light.color_ * attenuation;

        float NdotL = saturate(dot(N, L));
        float NdotH = saturate(dot(N, H));
        float HdotV = saturate(dot(H, V));

        // Cook-Torrance specular
        float  D = D_GGX(NdotH, roughness);
        float  G = G_Smith(NdotV, NdotL, roughness);
        float3 F = F_Schlick(HdotV, F0);

        float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.0001f);

        // Diffuse lambertien : les métaux n'ont pas de diffuse
        float3 kD_light = (1.0f - F) * (1.0f - metallic);
        float3 diffuse = kD_light * albedo / PI;

        color += (diffuse + specular) * radiance * NdotL;
    }

    return float4(saturate(color), 1.0f);
}
