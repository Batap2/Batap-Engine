#include "Lighting.hlsli"
#include "Terrain.hlsli"

float4 main(TerrainVSOutput i) : SV_Target
{
    const TerrainChunkGPUData chunk = TerrainChunkBuffer[g_draw.instanceIndex_];
    const uint matIdx = chunk.materialIndices_[g_draw.submeshIndex_];
    const Material mat = MaterialBuffer[matIdx != InvalidGPUIndex ? matIdx : 0u];

    // No uv on the terrain yet: the maps are read at their corner, and no
    // normal map without a tangent.
    const float2 uv = float2(0.0f, 0.0f);
    Surface s;
    s.albedo_ = mat.albedo.rgb * g_textures[mat.albedoTexIdx_].Sample(g_sampler, uv).rgb;
    s.roughness_ =
        clamp(mat.roughness * g_textures[mat.roughnessTexIdx_].Sample(g_sampler, uv).r, 0.05f, 1.0f);
    s.metallic_ = saturate(mat.metallic * g_textures[mat.metallicTexIdx_].Sample(g_sampler, uv).r);
    s.reflectivity_ = mat.reflectivity;
    s.posWS_ = i.posWS_;
    s.Ngeom_ = normalize(i.nrmWS_);
    s.N_ = s.Ngeom_;

    return float4(saturate(ShadeSurface(mat.shadingModel_, s)), 1.0f);
}
