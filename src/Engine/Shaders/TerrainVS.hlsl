#include "ShaderInterop.h"
#include "Terrain.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

TerrainVSOutput main(uint vertexId : SV_VertexID)
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const TerrainChunkGPUData chunk = TerrainChunkBuffer[g_draw.instanceIndex_];
    const float4x4 world = TerrainWorldBuffer[g_draw.instanceIndex_].world_;

    float3 position;
    float3 normal;
    terrainVertex(chunk, world, vertexId, position, normal);

    // Camera-relative all the way to the view: the view's own translation is
    // the camera's position, a float a millimetre wide far out.
    const float3 relative = mul(world, float4(position, 1.0f)).xyz;
    TerrainVSOutput o;
    o.posWS_ = cam.pos_ + relative;
    o.nrmWS_ = normalize(mul((float3x3)world, normal));
    o.position_ = mul(cam.proj_, float4(mul((float3x3)cam.view_, relative), 1.0f));
    return o;
}
