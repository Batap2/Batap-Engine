#include "MeshInstances.hlsli"
#include "Shadows.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION;
    float  cut_      : SV_ClipDistance0;
};

// Relative to the render camera, as the light matrices are built.
VS_OUTPUT main(float3 position_ : POSITION)
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const StaticMeshGPUData inst = StaticMeshInstancebuffer[g_draw.instanceIndex_];
    const float3 posRel =
        (inst.world_._m03_m13_m23 - cam.pos_) + mul((float3x3)inst.world_, position_);
    VS_OUTPUT o;
    o.position_ = mul(ShadowBuffer[g_draw.shadowViewIndex_].viewProj_, float4(posRel, 1.0f));
    o.cut_ = MeshCutDistance(inst, position_);
    return o;
}
