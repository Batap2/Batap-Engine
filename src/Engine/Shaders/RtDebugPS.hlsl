#include "Lighting.hlsli"
#include "Terrain.hlsli"

[[vk::binding(InstancesBinding, FrameSet)]]
StructuredBuffer<StaticMeshGPUData> StaticMeshInstancebuffer;
[[vk::binding(RtGeometriesBinding, RayTracingSet)]]
StructuredBuffer<RtGeometryGPUData> RtGeometryBuffer;
[[vk::binding(RtInstancesBinding, RayTracingSet)]]
StructuredBuffer<RtInstanceGPUData> RtInstanceBuffer;

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

float4 main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0) : SV_Target
{
    CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];

    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, 0.0f);
    ray.Direction = normalize((ndc.x / cam.proj_[0][0]) * cam.right_ +
                              (ndc.y / cam.proj_[1][1]) * cam.up_ + cam.fwd_);
    ray.TMin = cam.znear_;
    ray.TMax = 1.0e8f;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(Tlas, RAY_FLAG_NONE, TlasMaskShadowCaster | TlasMaskNoShadow, ray);
    while (query.Proceed())
    {
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    RtInstanceGPUData inst = RtInstanceBuffer[query.CommittedInstanceIndex()];
    uint geometryIndex = query.CommittedGeometryIndex();
    RtGeometryGPUData geom = RtGeometryBuffer[inst.firstGeometry_ + geometryIndex];
    uint64_t indices = RtAddress(geom.indicesAddrLo_, geom.indicesAddrHi_);
    uint64_t vertices = RtAddress(geom.verticesAddrLo_, geom.verticesAddrHi_);
    uint first = query.CommittedPrimitiveIndex() * 3u;
    float3x4 objectToWorld = query.CommittedObjectToWorld3x4();
    float3 p[3];
    [unroll]
    for (uint k = 0; k < 3; ++k)
    {
        uint index = RtIndex(indices, geom.indexBytes_, first + k);
        p[k] = mul(objectToWorld, float4(RtPosition(vertices, geom.vertexStride_, index), 1.0f));
    }
    float3 n = normalize(cross(p[1] - p[0], p[2] - p[0]));

    uint matIdx = inst.kind_ == RtInstanceChunk
                      ? TerrainChunkBuffer[inst.gpuIndex_].materialIndices_[geometryIndex]
                      : StaticMeshInstancebuffer[inst.gpuIndex_].materialIndices_[geometryIndex];
    Material mat = MaterialBuffer[matIdx != InvalidGPUIndex ? matIdx : 0u];

    return ndc.x < 0.0f ? float4(mat.albedo.rgb, 1.0f) : float4(n * 0.5f + 0.5f, 1.0f);
}
