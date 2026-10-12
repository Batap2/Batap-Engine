#include "ShaderInterop.h"
#include "Shadows.hlsli"
#include "Terrain.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

// The matrices and the morph follow the camera the scene is drawn from, not
// the light, so the shadow falls from the shape the eye sees.
float4 main(uint vertexId : SV_VertexID) : SV_POSITION
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const TerrainChunkGPUData chunk = TerrainChunkBuffer[g_draw.instanceIndex_];
    const float4x4 world = TerrainWorldBuffer[g_draw.instanceIndex_].world_;
    float3 position;
    float3 normal;
    terrainVertex(chunk, world, vertexId, position, normal);
    const float3 posWS = cam.pos_ + mul(world, float4(position, 1.0f)).xyz;
    return mul(ShadowBuffer[g_draw.shadowViewIndex_].viewProj_, float4(posWS, 1.0f));
}
