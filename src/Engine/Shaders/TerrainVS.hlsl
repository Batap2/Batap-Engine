#include "ShaderInterop.h"
#include "Terrain.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;

[[vk::binding(TerrainTracedBinding, FrameSet)]]
StructuredBuffer<float> TerrainTraced;

[[vk::push_constant]] DrawPush g_draw;

TerrainVSOutput main(uint vertexId : SV_VertexID)
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const TerrainChunkGPUData chunk = TerrainChunkBuffer[g_draw.instanceIndex_];
    const float4x4 world = TerrainWorldBuffer[g_draw.instanceIndex_].world_;

    float3 position;
    float3 normal;
    terrainVertex(chunk, world, vertexId, position, normal);
    // The positions the BLAS was built from: a shadow ray leaving this surface
    // must not hit the BLAS's, which may lag the morph.
    const uint traced = TerrainWorldBuffer[g_draw.instanceIndex_].tracedBase_;
    if (traced != TerrainNoTraced)
    {
        const uint at = (traced + vertexId) * 3u;
        position = float3(TerrainTraced[at], TerrainTraced[at + 1u], TerrainTraced[at + 2u]);
    }

    // Camera-relative all the way to the view: the view's own translation is
    // the camera's position, a float a millimetre wide far out.
    const float3 relative = mul(world, float4(position, 1.0f)).xyz;
    TerrainVSOutput o;
    o.posRel_ = relative;
    o.nrmWS_ = normalize(mul((float3x3)world, normal));
    o.position_ = mul(cam.proj_, float4(mul((float3x3)cam.view_, relative), 1.0f));
    o.grid_ = float2(vertexId % (chunk.cells_ + 1u), vertexId / (chunk.cells_ + 1u));
    return o;
}
