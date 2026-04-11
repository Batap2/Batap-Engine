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
    float4x4 _world;              // 64 bytes
    uint     _materialIndices[8]; // 32 bytes
    uint     _triangleOffsets[8]; // 32 bytes
    uint     _subMeshCount;       //  4 bytes
    uint     _pad[3];             // 12 bytes
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
    float4 _tangent  : TANGENT;   // xyz = tangent, w = handedness (±1)
};

struct VS_OUTPUT
{
    float4 _position : SV_POSITION; // clip space
    float3 _posWS    : TEXCOORD0;   // world position
    float3 _nrmWS    : TEXCOORD1;   // world normal
    float2 _uv       : TEXCOORD2;
    float4 _tanWS    : TEXCOORD3;   // xyz = world tangent, w = handedness
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
    float3x3 worldRot = (float3x3)inst._world;
    o._nrmWS = normalize(mul(worldRot, input._normal));

    // World tangent — preserve handedness in w
    float3 tanWS = normalize(mul(worldRot, input._tangent.xyz));
    o._tanWS = float4(tanWS, input._tangent.w);

    // Clip position
    float4 posVS = mul(cam._view, posWS4);
    o._position = mul(cam._proj, posVS);

    o._uv = input._uv;

    return o;
}