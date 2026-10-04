#include "ShaderInterop.h"
#include "Shadows.hlsli"
#include "Terrain.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

// The morph follows the camera the scene is drawn from, not the light, so
// the shadow falls from the shape the eye sees.
float4 main(uint vertexId : SV_VertexID) : SV_POSITION
{
    const TerrainChunkGPUData chunk = TerrainChunkBuffer[g_draw.instanceIndex_];
    float3 position;
    float3 normal;
    terrainVertex(chunk, vertexId, CameraInstancebuffer[g_draw.cameraIndex_].pos_, position, normal);
    return mul(ShadowBuffer[g_draw.shadowViewIndex_].viewProj_,
               mul(chunk.world_, float4(position, 1.0f)));
}
