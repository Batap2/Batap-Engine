#include "Lighting.hlsli"
#include "Reflections.hlsli"
#include "Terrain.hlsli"

struct PrepassOutput
{
    float4 normal_ : SV_Target0;
    uint2 surface_ : SV_Target1;
};

PrepassOutput main(TerrainVSOutput i)
{
    const TerrainChunkGPUData chunk = TerrainChunkBuffer[g_draw.instanceIndex_];
    const uint matIdx = chunk.materialIndices_[g_draw.submeshIndex_];
    const Material mat = MaterialBuffer[matIdx != InvalidGPUIndex ? matIdx : 0u];
    const float roughness = clamp(
        mat.roughness * g_textures[mat.roughnessTexIdx_].Sample(g_sampler, float2(0.0f, 0.0f)).r,
        0.05f, 1.0f);
    const float3 Ngeom = normalize(i.nrmWS_);
    const float3 N = (chunk.flags_ & TerrainFlat) != 0u ? FaceNormal(i.posRel_, Ngeom) : Ngeom;
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];

    PrepassOutput o;
    o.normal_ = float4(normalize(mul((float3x3)cam.view_, i.nrmWS_)) * 0.5f + 0.5f, 1.0f);
    o.surface_ = ReflectSurfaceEncode(N, ReflectRoughness(mat.shadingModel_, roughness),
                                      ReflectKindTerrain, g_draw.instanceIndex_);
    return o;
}
