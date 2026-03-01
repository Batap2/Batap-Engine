struct CameraData
{
    float4x4 _view;
    float4x4 _proj;
    float3 _pos;   float _znear;
    float3 _right; float _zfar;
    float3 _up;    float _fov;
};

StructuredBuffer<CameraData> CameraInstancebuffer : register(t0);

cbuffer DrawParams : register(b0)
{
    uint _cameraIndex;
    uint _instanceIndex;
};

struct VS_OUTPUT
{
    float4 _position : SV_POSITION; // clip space
    float3 _posWS    : TEXCOORD0;   // world position
    float3 _nrmWS    : TEXCOORD1;   // world normal
    float2 _uv       : TEXCOORD2;
};

float4 main(VS_OUTPUT i) : SV_Target
{
    CameraData cam = CameraInstancebuffer[_cameraIndex];

    // --------- paramètres Phong ----------
    float3 albedo    = float3(0.9, 0.9, 0.9);
    float  ka        = 0.08;   // ambient
    float  kd        = 1.00;   // diffuse
    float  ks        = 0.35;   // specular
    float  shininess = 32.0;   // exponent

    // Directional light (direction *vers* la lumière), en world space
    float3 L = normalize(float3(0.4, 1.0, 0.2));

    float3 N = normalize(i._nrmWS);

    // View vector: de la surface vers la caméra
    float3 V = normalize(cam._pos - i._posWS);

    // Ambient
    float3 ambient = ka * albedo;

    // Diffuse (Lambert)
    float NdotL = saturate(dot(N, L));
    float3 diffuse = kd * albedo * NdotL;

    // Specular (Phong)
    float3 R = reflect(-L, N);
    float spec = pow(saturate(dot(R, V)), shininess);
    float3 specular = ks * spec;

    float3 color = ambient + diffuse + specular;

    return float4(color, 1.0);
}
