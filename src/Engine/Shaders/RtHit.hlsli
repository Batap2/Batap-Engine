#ifndef BATAP_RTHIT_HLSLI
#define BATAP_RTHIT_HLSLI

#include "GiCache.hlsli"
#include "Lighting.hlsli"
#include "Terrain.hlsli"

[[vk::binding(InstancesBinding, FrameSet)]]
StructuredBuffer<StaticMeshGPUData> StaticMeshInstancebuffer;
[[vk::binding(RtGeometriesBinding, RayTracingSet)]]
StructuredBuffer<RtGeometryGPUData> RtGeometryBuffer;
[[vk::binding(RtInstancesBinding, RayTracingSet)]]
StructuredBuffer<RtInstanceGPUData> RtInstanceBuffer;

static const float RtHitFar = 1.0e8f;

struct RtHit
{
    float3 position_;
    float3 normal_;
    float3 albedo_;
    uint shadingModel_;
    GiPoint gi_;
};

uint64_t RtAddress(uint lo, uint hi)
{
    return (uint64_t(hi) << 32) | uint64_t(lo);
}

uint RtIndex(uint64_t indices, uint indexBytes, uint i)
{
    if (indexBytes == 4u)
        return vk::RawBufferLoad<uint>(indices + uint64_t(i) * 4u, 4u);
    uint64_t at = indices + uint64_t(i) * 2u;
    uint word = vk::RawBufferLoad<uint>(at & ~uint64_t(3u), 4u);
    return uint(at & 2u) != 0u ? word >> 16 : word & 0xFFFFu;
}

float3 RtPosition(uint64_t vertices, uint stride, uint index)
{
    uint64_t at = vertices + uint64_t(index) * uint64_t(stride);
    return float3(vk::RawBufferLoad<float>(at, 4u), vk::RawBufferLoad<float>(at + 4u, 4u),
                  vk::RawBufferLoad<float>(at + 8u, 4u));
}

RtHit RtResolveHit(RayQuery<RAY_FLAG_FORCE_OPAQUE> query, float3 rayDirection)
{
    const RtInstanceGPUData inst = RtInstanceBuffer[query.CommittedInstanceIndex()];
    const uint geometryIndex = query.CommittedGeometryIndex();
    const RtGeometryGPUData geom = RtGeometryBuffer[inst.firstGeometry_ + geometryIndex];
    const uint64_t indices = RtAddress(geom.indicesAddrLo_, geom.indicesAddrHi_);
    const uint64_t vertices = RtAddress(geom.verticesAddrLo_, geom.verticesAddrHi_);
    const uint first = query.CommittedPrimitiveIndex() * 3u;
    const float3x4 objectToWorld = query.CommittedObjectToWorld3x4();
    float3 p[3];
    [unroll]
    for (uint k = 0; k < 3; ++k)
    {
        const uint index = RtIndex(indices, geom.indexBytes_, first + k);
        p[k] = mul(objectToWorld, float4(RtPosition(vertices, geom.vertexStride_, index), 1.0f));
    }
    float3 n = normalize(cross(p[1] - p[0], p[2] - p[0]));
    if (dot(n, rayDirection) > 0.0f)
        n = -n;

    const uint matIdx = inst.kind_ == RtInstanceChunk
                            ? TerrainChunkBuffer[inst.gpuIndex_].materialIndices_[geometryIndex]
                            : StaticMeshInstancebuffer[inst.gpuIndex_].materialIndices_[geometryIndex];
    const Material mat = MaterialBuffer[matIdx != InvalidGPUIndex ? matIdx : 0u];

    RtHit hit;
    hit.position_ = query.WorldRayOrigin() + rayDirection * query.CommittedRayT();
    hit.normal_ = n;
    hit.albedo_ = mat.albedo.rgb;
    hit.shadingModel_ = mat.shadingModel_;
    if (inst.kind_ == RtInstanceChunk)
        hit.gi_ = GiTerrainPoint(TerrainWorldBuffer[inst.gpuIndex_], hit.position_, n);
    else
        hit.gi_ = GiMeshPoint(inst.gpuIndex_, (float3x3)objectToWorld, objectToWorld._m03_m13_m23,
                              hit.position_, n);
    return hit;
}

#endif
