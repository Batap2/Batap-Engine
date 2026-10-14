#include "ShaderInterop.h"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::binding(DebugShapeVertsBinding, FrameSet)]]
StructuredBuffer<DebugVertexGPUData> DebugShapeVerts;
[[vk::binding(DebugShapesBinding, FrameSet)]]
StructuredBuffer<DebugShapeGPUData> DebugShapes;

[[vk::push_constant]] DrawPush g_draw;

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION;
    float4 color_    : TEXCOORD0;
};

// SV_VertexID already carries the firstVertex of the draw, which is how one
// shared buffer holds every unit wireframe.
VS_OUTPUT main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    DebugShapeGPUData shape = DebugShapes[g_draw.instanceIndex_ + instanceId];

    float3 p = DebugShapeVerts[vertexId].pos_;
    // Divided by w because a frustum's matrix is projective; every other shape
    // is affine, where w comes back as 1 and this costs nothing.
    float4 posH = mul(shape.world_, float4(p, 1.0f));
    float3 posRel = posH.xyz / posH.w - cam.pos_;

    VS_OUTPUT o;
    o.position_ = mul(cam.proj_, float4(mul((float3x3)cam.view_, posRel), 1.0f));
    o.color_ = shape.color_;
    return o;
}
