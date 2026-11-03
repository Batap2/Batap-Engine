#ifndef BATAP_MESH_SURFACE_HLSLI
#define BATAP_MESH_SURFACE_HLSLI

#include "Lighting.hlsli"

[[vk::binding(InstancesBinding, FrameSet)]]
StructuredBuffer<StaticMeshGPUData> StaticMeshInstancebuffer;

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION;
    float3 posRel_   : TEXCOORD0;   // world axes, camera at the origin
    float3 nrmWS_    : TEXCOORD1;
    float2 uv_       : TEXCOORD2;
    float4 tanWS_    : TEXCOORD3;
};

Material MeshMaterial(StaticMeshGPUData inst)
{
    uint matIdx = inst.materialIndices_[g_draw.submeshIndex_];
    uint safeIdx = (matIdx != InvalidGPUIndex) ? matIdx : 0u;
    return MaterialBuffer[safeIdx];
}

Surface MeshSurface(VS_OUTPUT i, StaticMeshGPUData inst, Material mat, out float albedoAlpha)
{
    Surface s;
    const float4 albedoSample = g_textures[mat.albedoTexIdx_].Sample(g_sampler, i.uv_);
    albedoAlpha = mat.albedo.a * albedoSample.a;
    s.albedo_    = mat.albedo.rgb    * albedoSample.rgb;
    s.roughness_ = clamp(mat.roughness * g_textures[mat.roughnessTexIdx_].Sample(g_sampler, i.uv_).r, 0.05f, 1.0f);
    s.metallic_  = saturate(mat.metallic * g_textures[mat.metallicTexIdx_].Sample(g_sampler, i.uv_).r);
    s.reflectivity_ = mat.reflectivity;
    s.emissive_  = mat.emissive * mat.emissiveStrength;
    s.posRel_    = i.posRel_;

    float3 Ngeom = normalize(i.nrmWS_);
    s.Ngeom_ = Ngeom;
    s.Nface_ = FaceNormal(i.posRel_, Ngeom);
    s.cell_ = 0.0f;
    s.gi_ = GiMeshPoint(g_draw.instanceIndex_, (float3x3)inst.world_,
                        inst.world_._m03_m13_m23 - CameraInstancebuffer[g_draw.cameraIndex_].pos_,
                        i.posRel_, GiFacing(s.Nface_, i.posRel_));
    if (mat.normalTexIdx_ != InvalidGPUIndex)
    {
        float3 normalSample = g_textures[mat.normalTexIdx_].Sample(g_sampler, i.uv_).rgb;
        normalSample = normalSample * 2.0f - 1.0f;
        normalSample.z = sqrt(saturate(1.0f - dot(normalSample.xy, normalSample.xy)));

        float3 T = normalize(i.tanWS_.xyz);
        T = normalize(T - dot(T, Ngeom) * Ngeom);
        float3 B = cross(Ngeom, T) * i.tanWS_.w;
        s.N_ = normalize(normalSample.x * T + normalSample.y * B + normalSample.z * Ngeom);
    }
    else
    {
        s.N_ = Ngeom;
    }
    return s;
}

#endif
