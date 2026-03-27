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
    float2 _pad;
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
};

float4 main(VS_OUTPUT i, uint primId : SV_PrimitiveID) : SV_Target
{
    CameraData cam = CameraInstancebuffer[_cameraIndex];

    // --------- matériau ----------
    InstanceData inst = StaticMeshInstancebuffer[_instanceIndex];
    uint submesh = 0;
    [unroll]
    for (uint s = 1; s < inst._subMeshCount; ++s)
        if (primId >= inst._triangleOffsets[s]) submesh = s;

    uint   matIdx = inst._materialIndices[submesh];
    float3 albedo = (matIdx != 0xFFFFFFFFu)
                  ? MaterialBuffer[matIdx].albedo.rgb
                  : float3(0.9, 0.9, 0.9);
    float  ka        = 0.08;   // ambient
    float  kd        = 1.00;   // diffuse
    float  ks        = 0.35;   // specular
    float  shininess = 32.0;   // exponent

    float3 N = normalize(i._nrmWS);
    float3 V = normalize(cam._pos - i._posWS);

    // Ambient global
    float3 color = ka * albedo;

    // Accumulation de toutes les point lights
    [loop]
    for (uint lightIndex = 0; lightIndex < PointLightBufferSize; ++lightIndex)
    {
        PointLight light = PointLightBuffer[lightIndex];

        // vecteur surface -> lumière
        float3 toLight = light.pos_ - i._posWS;
        float  dist    = length(toLight);

        // éviter division par zéro
        if (dist > light.radius_ || dist <= 0.0001f)
            continue;

        float3 L = toLight / dist;

        // Atténuation basée sur la distance + radius
        // 1 à la source, 0 au radius
        float rangeAtt = saturate(1.0f - dist / light.radius_);

        // falloff contrôle la courbe
        float attenuation = pow(rangeAtt, max(light.falloff_, 0.0001f)) * light.intensity_;

        // Diffuse
        float NdotL = saturate(dot(N, L));
        float3 diffuse = kd * albedo * light.color_ * NdotL * attenuation;

        // Specular (Phong)
        float3 R = reflect(-L, N);
        float spec = pow(saturate(dot(R, V)), shininess);
        float3 specular = ks * light.color_ * spec * attenuation;

        color += diffuse + specular;
    }

    return float4(saturate(color), 1.0);
}