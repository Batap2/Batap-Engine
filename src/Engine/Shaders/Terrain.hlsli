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

float terrainRamp(float d, float start, float end)
{
    return saturate((d - start) / max(end - start, 1e-6f));
}

TerrainVertexGPUData terrainFetch(TerrainChunkGPUData chunk, uint i, uint j)
{
    return TerrainVertexBuffer[chunk.vertexBase_ + j * (chunk.cells_ + 1u) + i];
}

// Where the vertex shader puts grid vertex `vertexId` of a chunk, in the
// chunk's space. TerrainLod's morphVertex is the same on the CPU.
void terrainVertex(TerrainChunkGPUData chunk, uint vertexId, float3 cameraWS,
                   out float3 position, out float3 normal)
{
    const uint last = chunk.cells_;
    const uint i = vertexId % (last + 1u);
    const uint j = vertexId / (last + 1u);
    const TerrainVertexGPUData v = terrainFetch(chunk, i, j);
    position = v.position_;
    normal = terrainNormal(v.normal_);
    if ((chunk.flags_ & TerrainMorphs) == 0u)
        return;

    uint border = 0u;
    border |= (j == 0u) ? 1u : 0u;
    border |= (i == last) ? 2u : 0u;
    border |= (j == last) ? 4u : 0u;
    border |= (i == 0u) ? 8u : 0u;
    border |= (i == 0u && j == 0u) ? 16u : 0u;
    border |= (i == last && j == 0u) ? 32u : 0u;
    border |= (i == last && j == last) ? 64u : 0u;
    border |= (i == 0u && j == last) ? 128u : 0u;

    const float start2 = 2.0f * chunk.morphStart_;
    const float end2 = 2.0f * chunk.morphEnd_;
    const float3 target2WS = mul(chunk.world_, float4(v.target2_, 1.0f)).xyz;
    const float k2 = terrainRamp(length(target2WS - cameraWS), start2, end2);
    const float3 inner = lerp(v.target_, v.target2_, k2);
    const float3 innerNormal =
        lerp(terrainNormal(v.targetNormal_), terrainNormal(v.target2Normal_), k2);

    const uint pinned = border & chunk.force_;
    if (pinned == 0u)
    {
        const float3 targetWS = mul(chunk.world_, float4(v.target_, 1.0f)).xyz;
        const float k = terrainRamp(length(targetWS - cameraWS), chunk.morphStart_, chunk.morphEnd_);
        position = lerp(v.position_, inner, k);
        normal = lerp(normal, innerNormal, k);
        return;
    }
    normal = innerNormal;

    const bool oddI = (pinned & 5u) != 0u && (i & 1u) != 0u;
    const bool oddJ = (pinned & 10u) != 0u && (j & 1u) != 0u;
    if (!oddI && !oddJ)
    {
        position = inner;
        return;
    }

    // The coarser neighbour draws a straight segment between the two even
    // vertices around, each sliding on its own.
    uint2 ia = uint2(i, j - 1u);
    uint2 ib = uint2(i, j + 1u);
    if (oddI)
    {
        ia = uint2(i - 1u, j);
        ib = uint2(i + 1u, j);
    }
    const TerrainVertexGPUData a = terrainFetch(chunk, ia.x, ia.y);
    const TerrainVertexGPUData b = terrainFetch(chunk, ib.x, ib.y);
    const float kA =
        terrainRamp(length(mul(chunk.world_, float4(a.target2_, 1.0f)).xyz - cameraWS), start2, end2);
    const float kB =
        terrainRamp(length(mul(chunk.world_, float4(b.target2_, 1.0f)).xyz - cameraWS), start2, end2);
    position = 0.5f * (lerp(a.target_, a.target2_, kA) + lerp(b.target_, b.target2_, kB));
}

#endif
