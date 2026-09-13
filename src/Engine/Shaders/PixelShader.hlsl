// Modèle de binding Vulkan (docs/vulkan.md §10) :
//   set 0 = bindless global (sampler s0, textures t1[])
//   set 1 = données de frame (storage buffers)
//   push constants = indices du draw courant
#include "Lighting.hlsli"

[[vk::binding(InstancesBinding, FrameSet)]]
StructuredBuffer<StaticMeshGPUData> StaticMeshInstancebuffer;

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION; // clip space
    float3 posWS_    : TEXCOORD0;   // world position
    float3 nrmWS_    : TEXCOORD1;   // world normal
    float2 uv_       : TEXCOORD2;
    float4 tanWS_    : TEXCOORD3;   // xyz = world tangent, w = handedness
};

float4 main(VS_OUTPUT i) : SV_Target
{
    // --------- matériau ----------
    // Un draw par submesh, l'index arrive en push constant : SV_PrimitiveID
    // déclarerait la capability Geometry, absente sur Metal/MoltenVK.
    StaticMeshGPUData inst = StaticMeshInstancebuffer[g_draw.instanceIndex_];

    // matIdx 0xFFFFFFFF → slot 0 = default material (created at engine init)
    uint matIdx    = inst.materialIndices_[g_draw.submeshIndex_];
    uint safeIdx   = (matIdx != InvalidGPUIndex) ? matIdx : 0u;
    Material mat = MaterialBuffer[safeIdx];

    Surface s;
    // Texture channels always valid: unassigned → white/flat-normal texture (neutral multiplier)
    s.albedo_    = mat.albedo.rgb    * g_textures[mat.albedoTexIdx_].Sample(g_sampler, i.uv_).rgb;
    s.roughness_ = clamp(mat.roughness * g_textures[mat.roughnessTexIdx_].Sample(g_sampler, i.uv_).r, 0.05f, 1.0f);
    s.metallic_  = saturate(mat.metallic * g_textures[mat.metallicTexIdx_].Sample(g_sampler, i.uv_).r);
    s.reflectivity_ = mat.reflectivity;
    s.posWS_     = i.posWS_;

    float3 Ngeom = normalize(i.nrmWS_);
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

    return float4(saturate(ShadeSurface(mat.shadingModel_, s)), 1.0f);
}
