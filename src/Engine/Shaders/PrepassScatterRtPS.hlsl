#include "Lighting.hlsli"
#include "Reflections.hlsli"
#include "Scatter.hlsli"

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION;
    float3 posRel_   : TEXCOORD0;
    float3 nrmWS_    : TEXCOORD1;
    float2 uv_       : TEXCOORD2;
    float4 tanWS_    : TEXCOORD3;
    nointerpolation uint batch_ : TEXCOORD4;
};

struct PrepassOutput
{
    float4 normal_ : SV_Target0;
    uint2 surface_ : SV_Target1;
};

PrepassOutput main(VS_OUTPUT i)
{
    const ScatterBatchGPUData batch = ScatterBatchBuffer[i.batch_];
    const uint matIdx = batch.materialIndices_[g_draw.submeshIndex_];
    const Material mat = MaterialBuffer[matIdx != InvalidGPUIndex ? matIdx : 0u];
    const float roughness = clamp(
        mat.roughness * g_textures[mat.roughnessTexIdx_].Sample(g_sampler, i.uv_).r, 0.05f, 1.0f);
    const float3 Ngeom = normalize(i.nrmWS_);
    float3 N = Ngeom;
    if (mat.normalTexIdx_ != InvalidGPUIndex)
    {
        float3 normalSample = g_textures[mat.normalTexIdx_].Sample(g_sampler, i.uv_).rgb;
        normalSample = normalSample * 2.0f - 1.0f;
        normalSample.z = sqrt(saturate(1.0f - dot(normalSample.xy, normalSample.xy)));
        float3 T = normalize(i.tanWS_.xyz);
        T = normalize(T - dot(T, Ngeom) * Ngeom);
        const float3 B = cross(Ngeom, T) * i.tanWS_.w;
        N = normalize(normalSample.x * T + normalSample.y * B + normalSample.z * Ngeom);
    }
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];

    PrepassOutput o;
    o.normal_ = float4(normalize(mul((float3x3)cam.view_, i.nrmWS_)) * 0.5f + 0.5f, 1.0f);
    o.surface_ = ReflectSurfaceEncode(N, ReflectRoughness(mat.shadingModel_, roughness),
                                      ReflectKindScatter, i.batch_);
    return o;
}
