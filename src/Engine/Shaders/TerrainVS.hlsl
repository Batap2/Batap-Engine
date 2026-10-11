#include "ShaderInterop.h"
#include "Terrain.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;

[[vk::push_constant]] DrawPush g_draw;

TerrainVSOutput main(uint vertexId : SV_VertexID)
{
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const TerrainChunkGPUData chunk = TerrainChunkBuffer[g_draw.instanceIndex_];
    const TerrainVertexGPUData v = terrainVertex(chunk, vertexId);

    TerrainVSOutput o;
    const float4 posWS = mul(chunk.world_, float4(v.position_, 1.0f));
    o.posWS_ = posWS.xyz;
    o.nrmWS_ = normalize(mul((float3x3)chunk.world_, terrainNormal(v.normal_)));
    o.position_ = mul(cam.proj_, mul(cam.view_, posWS));
    return o;
}
