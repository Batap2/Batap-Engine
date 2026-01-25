struct CameraData
{
    float3  _pos;
    float   _znear;
    float3  _forward;
    float   _zfar;
    float3  _right;
    float   _fov;
    float4x4 _view;
    float4x4 _proj;
};

struct InstanceData
{
    float4x4 _world;
    //float3x3 _normalMatrix;
};

StructuredBuffer<CameraData> CameraInstancebuffer :register(t0);
StructuredBuffer<InstanceData> StaticMeshInstancebuffer : register(t1);

cbuffer DrawParams : register(b0)
{
    uint _cameraIndex;
    uint _instanceIndex;
};

struct VS_INPUT {
    float3 _position : POSITION;
    float3 _normal : NORMAL;
    float2 _uv : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 _position : SV_POSITION;
    float3 _normal : TEXCOORD0;
    float2 _uv : TEXCOORD1;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;

    InstanceData inst = StaticMeshInstancebuffer[_instanceIndex];
    CameraData cam = CameraInstancebuffer[_cameraIndex];

    float4 posWS = mul(float4(input._position, 1.0f), inst._world);
    float4 posVS = mul(posWS, cam._view);
    output._position   = mul(posVS, cam._proj);

    //output._normal = normalize(mul(input._normal, inst._normalMatrix));
    output._normal = input._normal;
    output._uv = input._uv;

    return output;
}