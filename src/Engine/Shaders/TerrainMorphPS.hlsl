#include "ShaderInterop.h"
#include "Terrain.hlsli"

[[vk::binding(TerrainMorphRowsBinding, FrameSet)]]
StructuredBuffer<TerrainMorphRowGPUData> TerrainMorphRows;
[[vk::binding(TerrainTracedBinding, FrameSet)]]
RWStructuredBuffer<float> TerrainTraced;

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    const uint2 pixel = uint2(svpos.xy);
    const TerrainMorphRowGPUData row = TerrainMorphRows[pixel.y];
    const uint vertex = row.firstVertex_ + pixel.x;
    if (vertex >= row.vertices_)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);

    float3 position;
    float3 normal;
    terrainVertex(TerrainChunkBuffer[row.instance_], TerrainWorldBuffer[row.instance_].world_,
                  vertex, position, normal);
    const uint at = (row.traced_ + vertex) * 3u;
    TerrainTraced[at] = position.x;
    TerrainTraced[at + 1u] = position.y;
    TerrainTraced[at + 2u] = position.z;
    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}
