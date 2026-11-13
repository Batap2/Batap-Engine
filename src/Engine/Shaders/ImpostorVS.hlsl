#include "Impostor.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

static const float2 ImpostorCorners[6] = {
    float2(-1.0f, -1.0f), float2(1.0f, -1.0f), float2(1.0f, 1.0f),
    float2(-1.0f, -1.0f), float2(1.0f, 1.0f),  float2(-1.0f, 1.0f),
};

ImpostorVSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const uint instance = ScatterInstanceOf(instanceId, g_draw.instanceIndex_);
    const uint batchIndex = ScatterBatchOf(instance);
    const ScatterBatchGPUData batch = ScatterBatchBuffer[batchIndex];
    const ScatterInstanceGPUData inst = ScatterInstanceBuffer[instance];
    const float4x4 world = ScatterPlaced(batch).world_;
    const float3x3 placed = (float3x3)world;
    const float3 centre = world._m03_m13_m23 + mul(placed, inst.position_);

    const float4 inverse = float4(-inst.rotation_.xyz, inst.rotation_.w);
    const float3 toEye = normalize(ScatterRotate(inverse, mul(transpose(placed), -centre)));
    const uint2 cell = ImpostorCell(toEye);
    float3 right;
    float3 up;
    ImpostorAxes(OctDecode((float2(cell) + 0.5f) / float(ImpostorGrid)), right, up);

    float scale = inst.scale_;
    if (batch.fadeEnd_ > 0.0f)
        scale *= saturate((batch.fadeEnd_ - length(centre)) /
                          max(batch.fadeEnd_ - batch.fadeStart_, 1e-3f));
    const float2 corner = ImpostorCorners[vertexId];
    const float3 local = (right * corner.x + up * corner.y) * (batch.impostorRadius_ * scale);

    ImpostorVSOutput o;
    o.posRel_ = centre + mul(placed, ScatterRotate(inst.rotation_, local));
    o.position_ = mul(cam.proj_, float4(mul((float3x3)cam.view_, o.posRel_), 1.0f));
    o.uv_ = (float2(cell) + float2(corner.x, -corner.y) * 0.5f + 0.5f) / float(ImpostorGrid);
    o.rotation_ = inst.rotation_;
    o.batch_ = batchIndex;
    return o;
}
