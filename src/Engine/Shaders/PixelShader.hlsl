struct CameraData
{
    float4x4 _view;
    float4x4 _proj;
    float3 _pos;
    float _znear;
    float3 _right;
    float _zfar;
    float3 _up;
    float _fov;
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
    uint   albedoTexIdx;      // 0xFFFFFFFF = no texture
    uint   normalTexIdx;
    uint   roughnessTexIdx;
    uint   metallicTexIdx;
    uint2  _pad;
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

SamplerState      g_sampler    : register(s0);
Texture2D<float4> g_textures[] : register(t4);

static const float PI = 3.14159265358979f;

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

    // Ambient approximatif (sera remplacé par IBL plus tard)
    float3 color = 0.03f * albedo * (1.0f - metallic);

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