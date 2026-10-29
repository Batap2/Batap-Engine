#include "Lighting.hlsli"
#include "Terrain.hlsli"

// DebugDraw's colours, one per level.
static const float3 TerrainLevelColours[8] = {
    float3(1.0f, 1.0f, 1.0f),   float3(1.0f, 0.9f, 0.2f),  float3(0.35f, 1.0f, 0.35f),
    float3(0.3f, 0.9f, 1.0f),   float3(0.35f, 0.5f, 1.0f), float3(1.0f, 0.3f, 0.9f),
    float3(1.0f, 0.25f, 0.25f), float3(0.5f, 0.5f, 0.55f),
};

float4 main(TerrainVSOutput i) : SV_Target
{
    const TerrainChunkGPUData chunk = TerrainChunkBuffer[g_draw.instanceIndex_];

    // The flag is the same for the whole draw, so fwidth is defined inside.
    if ((chunk.flags_ & TerrainOutline) != 0u)
    {
        const float2 pixels =
            min(i.grid_, float(chunk.cells_) - i.grid_) / max(fwidth(i.grid_), 1e-6f);
        if (min(pixels.x, pixels.y) < 1.5f)
            return float4(TerrainLevelColours[chunk.level_ % 8u], 1.0f);
    }

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
    s.posRel_ = i.posRel_;
    s.Ngeom_ = normalize(i.nrmWS_);
    s.Nface_ = FaceNormal(i.posRel_, s.Ngeom_);
    s.N_ = s.Ngeom_;
    s.gi_ = GiTerrainPoint(TerrainWorldBuffer[g_draw.instanceIndex_], i.posRel_,
                           GiFacing(s.Nface_, i.posRel_));

    return float4(ShadeSurface(mat.shadingModel_, s), 1.0f);
}
