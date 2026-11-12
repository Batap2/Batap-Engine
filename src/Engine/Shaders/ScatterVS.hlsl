#include "Scatter.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

struct VS_INPUT
{
    float3 position_ : POSITION;
    float3 normal_   : NORMAL;
    float2 uv_       : TEXCOORD0;
    float4 tangent_  : TANGENT;
};

struct VS_OUTPUT
{
    // Invariant (18), as in VertexShader: the prepass and the lit pass test
    // depth EQUAL.
    [[vk::ext_decorate(18)]] float4 position_ : SV_POSITION;
    float3 posRel_   : TEXCOORD0;
    float3 nrmWS_    : TEXCOORD1;
    float2 uv_       : TEXCOORD2;
    float4 tanWS_    : TEXCOORD3;
};

VS_OUTPUT main(VS_INPUT input, uint instanceId : SV_InstanceID)
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const ScatterBatchGPUData batch = ScatterBatchBuffer[g_draw.instanceIndex_];
    const ScatterInstanceGPUData inst = ScatterInstance(batch, instanceId);
    const TerrainWorldGPUData placed = ScatterPlaced(batch);

    VS_OUTPUT o;
    o.posRel_ = ScatterPosRel(batch, placed, inst, input.position_);
    o.nrmWS_ = ScatterDirection(placed, inst, input.normal_);
    o.tanWS_ = float4(ScatterDirection(placed, inst, input.tangent_.xyz), input.tangent_.w);
    o.position_ = mul(cam.proj_, float4(mul((float3x3)cam.view_, o.posRel_), 1.0f));
    o.uv_ = input.uv_;
    return o;
}
