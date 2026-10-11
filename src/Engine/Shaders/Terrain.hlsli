#ifndef BATAP_TERRAIN_HLSLI
#define BATAP_TERRAIN_HLSLI

#include "ShaderInterop.h"

[[vk::binding(TerrainChunksBinding, FrameSet)]]
StructuredBuffer<TerrainChunkGPUData> TerrainChunkBuffer;
[[vk::binding(TerrainVerticesBinding, FrameSet)]]
StructuredBuffer<TerrainVertexGPUData> TerrainVertexBuffer;

struct TerrainVSOutput
{
    float4 position_ : SV_POSITION;
    float3 posWS_    : TEXCOORD0;
    float3 nrmWS_    : TEXCOORD1;
};

// TerrainChunk.cpp's unpackNormal.
float3 terrainNormal(uint packed)
{
    const int2 q = int2(packed << 16, packed) >> 16;
    const float2 e = max(float2(q) / 32767.0f, -1.0f);
    float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
    const float t = saturate(-n.z);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return normalize(n);
}

// The indices are local to a chunk, so `vertexId` is the grid index.
TerrainVertexGPUData terrainVertex(TerrainChunkGPUData chunk, uint vertexId)
{
    return TerrainVertexBuffer[chunk.vertexBase_ + vertexId];
}

#endif
