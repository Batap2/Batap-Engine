// Modèle de binding Vulkan (docs/vulkan.md §10) :
//   set 0 = bindless global (sampler, textures) — pas utilisé ici
//   set 1 = données de frame (storage buffers : caméras, instances, …)
//   push constants = indices du draw courant
#include "ShaderInterop.h"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::binding(InstancesBinding, FrameSet)]]
StructuredBuffer<StaticMeshGPUData> StaticMeshInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

struct VS_INPUT
{
    float3 position_ : POSITION;
    float3 normal_   : NORMAL;
    float2 uv_       : TEXCOORD0;
    float4 tangent_  : TANGENT;   // xyz = tangent, w = handedness (±1)
};

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION; // clip space
    float3 posWS_    : TEXCOORD0;   // world position
    float3 nrmWS_    : TEXCOORD1;   // world normal
    float2 uv_       : TEXCOORD2;
    float4 tanWS_    : TEXCOORD3;   // xyz = world tangent, w = handedness
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT o;

    CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    StaticMeshGPUData inst = StaticMeshInstancebuffer[g_draw.instanceIndex_];

    // World position
    float4 posWS4 = mul(inst.world_, float4(input.position_, 1.0f));
    o.posWS_ = posWS4.xyz;

    // World normal (OK si pas de non-uniform scale ; sinon inverse-transpose)
    float3x3 worldRot = (float3x3)inst.world_;
    o.nrmWS_ = normalize(mul(worldRot, input.normal_));

    // World tangent — preserve handedness in w
    float3 tanWS = normalize(mul(worldRot, input.tangent_.xyz));
    o.tanWS_ = float4(tanWS, input.tangent_.w);

    // Clip position
    float4 posVS = mul(cam.view_, posWS4);
    o.position_ = mul(cam.proj_, posVS);

    o.uv_ = input.uv_;

    return o;
}
