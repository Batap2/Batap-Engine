#include "Lighting.hlsli"
#include "Motion.hlsli"
#include "Scatter.hlsli"

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION;
    float3 posRel_   : TEXCOORD0;
    float3 nrmWS_    : TEXCOORD1;
    float2 uv_       : TEXCOORD2;
    float4 tanWS_    : TEXCOORD3;
};

float4 main(VS_OUTPUT i) : SV_Target
{
    const ScatterBatchGPUData batch = ScatterBatchBuffer[g_draw.instanceIndex_];
    const uint matIdx = batch.materialIndices_[g_draw.submeshIndex_];
    const Material mat = MaterialBuffer[matIdx != InvalidGPUIndex ? matIdx : 0u];

    Surface s;
    s.albedo_ = mat.albedo.rgb * g_textures[mat.albedoTexIdx_].Sample(g_sampler, i.uv_).rgb;
    s.roughness_ =
        clamp(mat.roughness * g_textures[mat.roughnessTexIdx_].Sample(g_sampler, i.uv_).r, 0.05f, 1.0f);
    s.metallic_ = saturate(mat.metallic * g_textures[mat.metallicTexIdx_].Sample(g_sampler, i.uv_).r);
    s.reflectivity_ = mat.reflectivity;
    s.emissive_ = mat.emissive * mat.emissiveStrength;
    s.posRel_ = i.posRel_;

    const float3 Ngeom = normalize(i.nrmWS_);
    s.Ngeom_ = Ngeom;
    s.Nface_ = FaceNormal(i.posRel_, Ngeom);
    s.cell_ = 0.0f;
    s.gi_ = GiTerrainPoint(batch.placed_, i.posRel_, GiFacing(s.Nface_, i.posRel_));
    if (mat.normalTexIdx_ != InvalidGPUIndex)
    {
        float3 normalSample = g_textures[mat.normalTexIdx_].Sample(g_sampler, i.uv_).rgb;
        normalSample = normalSample * 2.0f - 1.0f;
        normalSample.z = sqrt(saturate(1.0f - dot(normalSample.xy, normalSample.xy)));

        float3 T = normalize(i.tanWS_.xyz);
        T = normalize(T - dot(T, Ngeom) * Ngeom);
        const float3 B = cross(Ngeom, T) * i.tanWS_.w;
        s.N_ = normalize(normalSample.x * T + normalSample.y * B + normalSample.z * Ngeom);
    }
    else
    {
        s.N_ = Ngeom;
    }

    if ((g_draw.debugFlags_ & DebugMotionVectors) != 0u)
    {
        const float3 prevRel =
            PreviousPosition(i.posRel_, batch.placed_.world_, batch.placed_.prevWorld_);
        return float4(MotionDebugColor(MotionNdc(CameraInstancebuffer[g_draw.cameraIndex_],
                                                 i.posRel_, prevRel)),
                      1.0f);
    }
    return float4(ShadeSurface(mat.shadingModel_, s), 1.0f);
}
