#include "Lighting.hlsli"
#include "Impostor.hlsli"
#ifdef IMPOSTOR_REFLECT
#include "Reflections.hlsli"
#endif

struct PrepassOutput
{
    float4 normal_ : SV_Target0;
#ifdef IMPOSTOR_REFLECT
    uint2 surface_ : SV_Target1;
#endif
};

PrepassOutput main(ImpostorVSOutput i)
{
    const ScatterBatchGPUData batch = ScatterBatchBuffer[g_draw.instanceIndex_];
    const uint matIdx = batch.materialIndices_[0];
    const Material mat = MaterialBuffer[matIdx != InvalidGPUIndex ? matIdx : 0u];

    clip(g_textures[mat.albedoTexIdx_].Sample(g_sampler, i.uv_).a - ImpostorAlphaCut);
    const float3 N = ImpostorNormal(
        batch, i.rotation_, g_textures[mat.normalTexIdx_].Sample(g_sampler, i.uv_).xyz * 2.0f - 1.0f);

    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    PrepassOutput o;
    o.normal_ = float4(normalize(mul((float3x3)cam.view_, N)) * 0.5f + 0.5f, 1.0f);
#ifdef IMPOSTOR_REFLECT
    o.surface_ = ReflectSurfaceEncode(
        N, ReflectRoughness(mat.shadingModel_, clamp(mat.roughness, 0.05f, 1.0f)), ReflectKindScatter,
        g_draw.instanceIndex_);
#endif
    return o;
}
