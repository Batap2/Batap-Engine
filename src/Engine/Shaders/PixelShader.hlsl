struct CameraData
{
    float4x4 _view;
    float4x4 _proj;
    float3 _pos;   float _znear;
    float3 _right; float _zfar;
    float3 _up;    float _fov;
    float3 _fwd;   float _pad;
};

StructuredBuffer<CameraData> CameraInstancebuffer : register(t0);

struct InstanceData
{
    float4x4 _world;              // 64 bytes
    uint     _materialIndices[8]; // 32 bytes
    uint     _triangleOffsets[8]; // 32 bytes
    uint     _subMeshCount;       //  4 bytes
    uint     _pad[3];             // 12 bytes
};
StructuredBuffer<InstanceData> StaticMeshInstancebuffer : register(t1);

struct PointLight
{
    float3 pos_;
    float intensity_;
    float3 color_;
    float radius_;
    float falloff_;
    bool castShadows_;
};
StructuredBuffer<PointLight> PointLightBuffer : register(t2);

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
    uint   _pad;
};
StructuredBuffer<MaterialData> MaterialBuffer : register(t3);

cbuffer DrawParams : register(b0)
{
    uint _cameraIndex;
    uint _instanceIndex;
    uint PointLightBufferSize;
};

struct VS_OUTPUT
{
    float4 _position : SV_POSITION; // clip space
    float3 _posWS    : TEXCOORD0;   // world position
    float3 _nrmWS    : TEXCOORD1;   // world normal
    float2 _uv       : TEXCOORD2;
    float4 _tanWS    : TEXCOORD3;   // xyz = world tangent, w = handedness
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
    float3 _pad;
};
StructuredBuffer<SkyboxGPUData> SkyboxBuffer : register(t0, space1);

SamplerState      g_sampler    : register(s0);
Texture2D<float4> g_textures[] : register(t4);

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

float4 main(VS_OUTPUT i, uint primId : SV_PrimitiveID) : SV_Target
{
    CameraData cam = CameraInstancebuffer[_cameraIndex];

    // --------- matériau ----------
    InstanceData inst = StaticMeshInstancebuffer[_instanceIndex];
    uint submesh = 0;
    [unroll]
    for (uint s = 1; s < inst._subMeshCount; ++s)
        if (primId >= inst._triangleOffsets[s]) submesh = s;

    // matIdx 0xFFFFFFFF → slot 0 = default material (created at engine init)
    uint matIdx    = inst._materialIndices[submesh];
    uint safeIdx   = (matIdx != 0xFFFFFFFFu) ? matIdx : 0u;
    MaterialData mat = MaterialBuffer[safeIdx];

    // Texture channels always valid: unassigned → white/flat-normal texture (neutral multiplier)
    float3 albedo    = mat.albedo.rgb    * g_textures[mat.albedoTexIdx].Sample(g_sampler, i._uv).rgb;
    float  roughness = clamp(mat.roughness * g_textures[mat.roughnessTexIdx].Sample(g_sampler, i._uv).r, 0.05f, 1.0f);
    float  metallic  = saturate(mat.metallic * g_textures[mat.metallicTexIdx].Sample(g_sampler, i._uv).r);

    // F0 : diélectrique = 0.04, métal = albedo
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float3 Ngeom = normalize(i._nrmWS);
    float3 N;
    if (mat.normalTexIdx != 0xFFFFFFFFu)
    {
        float3 normalSample = g_textures[mat.normalTexIdx].Sample(g_sampler, i._uv).rgb;
        normalSample = normalSample * 2.0f - 1.0f;
        normalSample.z = sqrt(saturate(1.0f - dot(normalSample.xy, normalSample.xy)));

        float3 T = normalize(i._tanWS.xyz);
        T = normalize(T - dot(T, Ngeom) * Ngeom);
        float3 B = cross(Ngeom, T) * i._tanWS.w;
        N = normalize(normalSample.x * T + normalSample.y * B + normalSample.z * Ngeom);
    }
    else
    {
        N = Ngeom;
    }
    float3 V = normalize(cam._pos - i._posWS);
    float  NdotV = saturate(dot(N, V));

    // IBL diffuse (SH L2) + spéculaire (env sampling)
    float3 F_ibl = F_Schlick(NdotV, F0);
    float3 kD    = (1.0f - F_ibl) * (1.0f - metallic);

    float3 R        = reflect(-V, N);
    float  mipLevel = roughness * float(max(SkyboxBuffer[0].mipCount, 1u) - 1u);
    float3 specIBL  = F_ibl * SampleSky(R, mipLevel) * mat.reflectivity;

    float3 color = kD * EvalSH9(N) * albedo + specIBL;

    [loop]
    for (uint lightIndex = 0; lightIndex < PointLightBufferSize; ++lightIndex)
    {
        PointLight light = PointLightBuffer[lightIndex];

        float3 toLight = light.pos_ - i._posWS;
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
        float3 kD = (1.0f - F) * (1.0f - metallic);
        float3 diffuse = kD * albedo / PI;

        color += (diffuse + specular) * radiance * NdotL;
    }

    return float4(saturate(color), 1.0);
}