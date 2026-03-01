struct CameraData
{
    float4x4 _view;
    float4x4 _proj;
    float3 _pos;   float _znear;
    float3 _right; float _zfar;
    float3 _up;    float _fov;
};

struct InstanceData
{
    float4x4 _world;
};

StructuredBuffer<CameraData>   CameraInstancebuffer     : register(t0);
StructuredBuffer<InstanceData> StaticMeshInstancebuffer : register(t1);

cbuffer DrawParams : register(b0)
{
    uint _cameraIndex;
    uint _instanceIndex;
};

struct VS_INPUT
{
    float3 _position : POSITION;
    float3 _normal   : NORMAL;
    float2 _uv       : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 _position : SV_POSITION; // clip space
    float3 _posWS    : TEXCOORD0;   // world position
    float3 _nrmWS    : TEXCOORD1;   // world normal
    float2 _uv       : TEXCOORD2;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT o;

    CameraData cam   = CameraInstancebuffer[_cameraIndex];
    InstanceData inst = StaticMeshInstancebuffer[_instanceIndex];

    // World position
    float4 posWS4 = mul(inst._world, float4(input._position, 1.0f));
    o._posWS = posWS4.xyz;

    // World normal (OK si pas de non-uniform scale ; sinon inverse-transpose)
    float3 nrmWS = mul((float3x3)inst._world, input._normal);
    o._nrmWS = normalize(nrmWS);

    // Clip position
    float4 posVS = mul(cam._view, posWS4);
    o._position = mul(cam._proj, posVS);

    o._uv = input._uv;

    return o;
}