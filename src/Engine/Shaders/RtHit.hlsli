#ifndef BATAP_RTHIT_HLSLI
#define BATAP_RTHIT_HLSLI

#include "GiCache.hlsli"
#include "Lighting.hlsli"
#include "MeshInstances.hlsli"
#include "Terrain.hlsli"

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
    float3 emissive_;
    uint shadingModel_;
    bool bounces_;
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
    hit.emissive_ = mat.emissive * mat.emissiveStrength;
    hit.shadingModel_ = mat.shadingModel_;
    hit.bounces_ = (inst.flags_ & RtInstanceNoGiBounce) == 0u;
    if (inst.kind_ == RtInstanceChunk)
        hit.gi_ = GiTerrainPoint(TerrainWorldBuffer[inst.gpuIndex_], hit.position_, n);
    else
        hit.gi_ = GiMeshPoint(inst.gpuIndex_, (float3x3)objectToWorld, objectToWorld._m03_m13_m23,
                              hit.position_, n);
    return hit;
}

struct RtSurface
{
    Surface surface_;
    uint shadingModel_;
    bool bounces_;
};

float3 RtLoad3(uint64_t at)
{
    return float3(vk::RawBufferLoad<float>(at, 4u), vk::RawBufferLoad<float>(at + 4u, 4u),
                  vk::RawBufferLoad<float>(at + 8u, 4u));
}

float4 RtSampleLevel(uint texture, float2 uv, float lodBase)
{
    uint width;
    uint height;
    g_textures[NonUniformResourceIndex(texture)].GetDimensions(width, height);
    return g_textures[NonUniformResourceIndex(texture)].SampleLevel(
        g_sampler, uv, lodBase + 0.5f * log2(max(float(width) * float(height), 1.0f)));
}

struct RtHitRecord
{
    uint instance_;
    uint geometry_;
    uint primitive_;
    float2 barycentrics_;
    float3x4 objectToWorld_;
    float3 position_;
    float t_;
};

RtHitRecord RtCommitted(RayQuery<RAY_FLAG_FORCE_OPAQUE> query)
{
    RtHitRecord r;
    r.instance_ = query.CommittedInstanceIndex();
    r.geometry_ = query.CommittedGeometryIndex();
    r.primitive_ = query.CommittedPrimitiveIndex();
    r.barycentrics_ = query.CommittedTriangleBarycentrics();
    r.objectToWorld_ = query.CommittedObjectToWorld3x4();
    r.t_ = query.CommittedRayT();
    r.position_ = query.WorldRayOrigin() + query.WorldRayDirection() * r.t_;
    return r;
}

RtHitRecord RtCommitted(RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> query)
{
    RtHitRecord r;
    r.instance_ = query.CommittedInstanceIndex();
    r.geometry_ = query.CommittedGeometryIndex();
    r.primitive_ = query.CommittedPrimitiveIndex();
    r.barycentrics_ = query.CommittedTriangleBarycentrics();
    r.objectToWorld_ = query.CommittedObjectToWorld3x4();
    r.t_ = query.CommittedRayT();
    r.position_ = query.WorldRayOrigin() + query.WorldRayDirection() * r.t_;
    return r;
}

RtSurface RtResolveSurface(RtHitRecord record, float3 rayDirection, float coneWidth)
{
    const RtInstanceGPUData inst = RtInstanceBuffer[record.instance_];
    const uint geometryIndex = record.geometry_;
    const RtGeometryGPUData geom = RtGeometryBuffer[inst.firstGeometry_ + geometryIndex];
    const uint64_t indices = RtAddress(geom.indicesAddrLo_, geom.indicesAddrHi_);
    const uint64_t vertices = RtAddress(geom.verticesAddrLo_, geom.verticesAddrHi_);
    const uint first = record.primitive_ * 3u;
    const float3x4 objectToWorld = record.objectToWorld_;
    const float3x3 rotation = (float3x3)objectToWorld;
    const float2 bary = record.barycentrics_;
    const float3 w = float3(1.0f - bary.x - bary.y, bary.x, bary.y);

    uint index[3];
    float3 p[3];
    [unroll]
    for (uint k = 0; k < 3; ++k)
    {
        index[k] = RtIndex(indices, geom.indexBytes_, first + k);
        p[k] = mul(objectToWorld, float4(RtPosition(vertices, geom.vertexStride_, index[k]), 1.0f));
    }
    const float3 across = cross(p[1] - p[0], p[2] - p[0]);
    const float worldArea = length(across);
    const float3 face = across / max(worldArea, 1e-30f);
    const float3 facing = dot(face, rayDirection) > 0.0f ? -face : face;
    const float footprint =
        log2(max(coneWidth, 1e-20f) / max(abs(dot(face, rayDirection)), 1e-3f));

    const bool chunk = inst.kind_ == RtInstanceChunk;
    const uint matIdx = chunk ? TerrainChunkBuffer[inst.gpuIndex_].materialIndices_[geometryIndex]
                              : StaticMeshInstancebuffer[inst.gpuIndex_].materialIndices_[geometryIndex];
    const Material mat = MaterialBuffer[matIdx != InvalidGPUIndex ? matIdx : 0u];

    Surface s = (Surface)0;
    s.posRel_ = record.position_;
    s.ior_ = mat.ior;
    s.diffuseRoughness_ = saturate(mat.diffuseRoughness);
    s.emissive_ = mat.emissive * mat.emissiveStrength;
    s.ao_ = 1.0f;
    s.screenShadow_ = 1.0f;

    float2 uv = float2(0.0f, 0.0f);
    float lodBase = 0.0f;
    float tangentSign = 1.0f;
    float3 T = float3(0.0f, 0.0f, 0.0f);
    float3 Ngeom = float3(0.0f, 0.0f, 0.0f);
    if (chunk)
    {
        const TerrainChunkGPUData chunkData = TerrainChunkBuffer[inst.gpuIndex_];
        const float4x4 placed = TerrainWorldBuffer[inst.gpuIndex_].world_;
        float2 grid[3];
        [unroll]
        for (uint k = 0; k < 3; ++k)
        {
            float3 unused;
            float3 normal;
            terrainVertex(chunkData, placed, index[k], unused, normal);
            Ngeom += w[k] * normalize(mul(rotation, normal));
            grid[k] = float2(index[k] % (chunkData.cells_ + 1u), index[k] / (chunkData.cells_ + 1u));
        }
        Ngeom = normalize(Ngeom);
        s.cell_ = length(p[1] - p[0]) / max(length(grid[1] - grid[0]), 1e-6f);
        s.gi_ = GiTerrainPoint(TerrainWorldBuffer[inst.gpuIndex_], s.posRel_, facing);
        lodBase = -1.0e4f;
    }
    else
    {
        const float3 nObject[3] = {RtLoad3(vertices + geom.normalsOffset_ + uint64_t(index[0]) * 12u),
                                   RtLoad3(vertices + geom.normalsOffset_ + uint64_t(index[1]) * 12u),
                                   RtLoad3(vertices + geom.normalsOffset_ + uint64_t(index[2]) * 12u)};
        float2 uvs[3];
        tangentSign = 0.0f;
        [unroll]
        for (uint k = 0; k < 3; ++k)
        {
            const uint64_t at =vertices + uint64_t(index[k]) * 8u + geom.uvsOffset_;
            uvs[k] = float2(vk::RawBufferLoad<float>(at, 4u), vk::RawBufferLoad<float>(at + 4u, 4u));
            const uint64_t tangentAt = vertices + uint64_t(index[k]) * 16u + geom.tangentsOffset_;
            Ngeom += w[k] * normalize(mul(rotation, nObject[k]));
            T += w[k] * normalize(mul(rotation, RtLoad3(tangentAt)));
            tangentSign += w[k] * vk::RawBufferLoad<float>(tangentAt + 12u, 4u);
            uv += w[k] * uvs[k];
        }
        Ngeom = normalize(Ngeom);
        const float2 du = uvs[1] - uvs[0];
        const float2 dv = uvs[2] - uvs[0];
        const float uvArea = abs(du.x * dv.y - du.y * dv.x);
        lodBase = 0.5f * log2(max(uvArea, 1e-30f) / max(worldArea, 1e-30f)) + footprint;
        s.gi_ = GiMeshPoint(inst.gpuIndex_, rotation, objectToWorld._m03_m13_m23, s.posRel_, facing);
    }

    const float4 albedoSample = RtSampleLevel(mat.albedoTexIdx_, uv, lodBase);
    s.albedo_ = mat.albedo.rgb * albedoSample.rgb;
    s.roughness_ =
        clamp(mat.roughness * RtSampleLevel(mat.roughnessTexIdx_, uv, lodBase).r, 0.05f, 1.0f);
    s.metallic_ = saturate(mat.metallic * RtSampleLevel(mat.metallicTexIdx_, uv, lodBase).r);
    s.Ngeom_ = Ngeom;
    s.Nface_ = dot(face, Ngeom) >= 0.0f ? face : -face;
    s.N_ = Ngeom;
    if (chunk && (TerrainChunkBuffer[inst.gpuIndex_].flags_ & TerrainFlat) != 0u)
        s.N_ = s.Nface_;
    if (!chunk && mat.normalTexIdx_ != InvalidGPUIndex)
    {
        float3 normalSample = RtSampleLevel(mat.normalTexIdx_, uv, lodBase).rgb;
        normalSample = normalSample * 2.0f - 1.0f;
        normalSample.z = sqrt(saturate(1.0f - dot(normalSample.xy, normalSample.xy)));
        float3 Tn = normalize(T);
        Tn = normalize(Tn - dot(Tn, Ngeom) * Ngeom);
        const float3 B = cross(Ngeom, Tn) * tangentSign;
        s.N_ = normalize(normalSample.x * Tn + normalSample.y * B + normalSample.z * Ngeom);
    }
    s.uv_ = uv;
    float3 tangent = T - dot(T, s.N_) * s.N_;
    if (!(dot(tangent, tangent) > 1e-8f))
        tangent = cross(abs(s.N_.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f),
                        s.N_);
    s.T_ = normalize(tangent);

    RtSurface hit;
    hit.surface_ = s;
    hit.shadingModel_ = mat.shadingModel_;
    hit.bounces_ = (inst.flags_ & RtInstanceNoGiBounce) == 0u;
    return hit;
}

// Unlit is shown as-is after exposure: brought back to radiance here.
float3 RtShadeHit(RtSurface hit, float3 V)
{
    if (hit.shadingModel_ == ShadingUnlit)
        return hit.surface_.albedo_ / max(FrameBuffer[0].exposure_, 1e-30f);
    return ShadeSurfaceToward(hit.shadingModel_, hit.surface_, V, 1.0f, false,
                              float4(0.0f, 0.0f, 0.0f, 0.0f));
}

#endif
