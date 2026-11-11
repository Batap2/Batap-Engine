#include "Lighting.hlsli"
#include "Impostor.hlsli"

float4 main(ImpostorVSOutput i) : SV_Target
{
    const ScatterBatchGPUData batch = ScatterBatchBuffer[g_draw.instanceIndex_];
    const uint matIdx = batch.materialIndices_[0];
    const Material mat = MaterialBuffer[matIdx != InvalidGPUIndex ? matIdx : 0u];

    const float4 albedo = g_textures[mat.albedoTexIdx_].Sample(g_sampler, i.uv_);
    clip(albedo.a - ImpostorAlphaCut);
    const float3 N = ImpostorNormal(
        batch, i.rotation_, g_textures[mat.normalTexIdx_].Sample(g_sampler, i.uv_).xyz * 2.0f - 1.0f);

    Surface s;
    s.albedo_ = mat.albedo.rgb * albedo.rgb;
    s.roughness_ = clamp(mat.roughness, 0.05f, 1.0f);
    s.metallic_ = saturate(mat.metallic);
    s.ior_ = mat.ior;
    s.diffuseRoughness_ = saturate(mat.diffuseRoughness);
    s.emissive_ = mat.emissive * mat.emissiveStrength;
    s.glintDensity_ = 0.0f;
    s.posRel_ = i.posRel_;
    s.Ngeom_ = N;
    s.Nface_ = N;
    s.N_ = N;
    s.cell_ = 0.0f;
    ApplyScreenAo(s, i.position_);
    s.gi_ = GiTerrainPoint(batch.placed_, i.posRel_, GiFacing(N, i.posRel_));
    return float4(
        ShadeSurface(mat.shadingModel_, s, 1.0f, ScreenReflection(i.position_, s.roughness_)), 1.0f);
}
