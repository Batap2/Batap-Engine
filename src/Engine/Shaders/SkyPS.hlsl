// Modèle de binding Vulkan (docs/vulkan.md §10).
// Changement vs DX12 : les paramètres du ciel (mode, couleurs, HDRI) étaient
// recopiés du Skybox_C en 16 root constants — ils sont déjà dans le
// SkyboxBuffer que le PixelShader utilise pour l'IBL. Une source de vérité.

struct CameraData
{
    float4x4 _view;
    float4x4 _proj;
    float3   _pos;   float _znear;
    float3   _right; float _zfar;
    float3   _up;    float _fov;
    float3   _fwd;   float _pad;
};
[[vk::binding(0, 1)]] StructuredBuffer<CameraData> CameraBuffer : register(t0);

struct SkyboxGPUData
{
    float4 sh[9];
    uint   mode;           // 0=HDRI  1=FlatColor  2=Gradient
    uint   heapIdx;        // bindless HDRI (0xFFFFFFFF = absent)
    uint   mipCount;
    float  intensity;
    float4 color1;         // zenith ; flat : couleur unique
    float4 color2;         // horizon
    float4 color3;         // nadir
    float  horizonWidth;   // gradient : largeur de la bande horizon
    float3 _pad;
};
[[vk::binding(4, 1)]] StructuredBuffer<SkyboxGPUData> SkyboxBuffer : register(t1);

struct DrawPush
{
    uint _cameraIndex;
    uint _instanceIndex;    // inutilisé ici — layout partagé avec la geometry
    uint _submeshIndex;     // idem
    uint _pointLightCount;  // idem
};
[[vk::push_constant]] DrawPush g_draw;

[[vk::binding(0, 0)]] SamplerState      g_sampler    : register(s0);
[[vk::binding(1, 0)]] Texture2D<float4> g_textures[] : register(t2);

static const float PI = 3.14159265358979f;

float4 main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0) : SV_Target
{
    CameraData cam = CameraBuffer[g_draw._cameraIndex];
    SkyboxGPUData sky = SkyboxBuffer[0];

    float3 dir = normalize(
        (ndc.x / cam._proj[0][0]) * cam._right +
        (ndc.y / cam._proj[1][1]) * cam._up    +
        cam._fwd);

    float3 color;

    if (sky.mode == 0u && sky.heapIdx != 0xFFFFFFFFu)  // HDRI
    {
        float phi   = atan2(dir.z, dir.x);
        float theta = asin(clamp(dir.y, -1.0f, 1.0f));
        float2 uv   = float2((phi + PI) / (2.0f * PI), 0.5f - theta / PI);
        color = g_textures[sky.heapIdx].Sample(g_sampler, uv).rgb;
    }
    else if (sky.mode == 1u)  // FlatColor
    {
        color = sky.color1.rgb;
    }
    else  // Gradient : ciel → horizon → bas
    {
        float safeWidth  = max(sky.horizonWidth, 0.01f);
        float upBlend    = smoothstep(0.0f, safeWidth, dir.y);
        float downBlend  = smoothstep(0.0f, safeWidth, -dir.y);
        color = lerp(sky.color2.rgb, sky.color1.rgb,  upBlend);
        color = lerp(color,          sky.color3.rgb,  downBlend);
    }

    return float4(color, 1.0f);
}
