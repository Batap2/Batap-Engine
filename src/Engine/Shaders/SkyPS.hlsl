struct CameraData
{
    float4x4 _view;
    float4x4 _proj;
    float3   _pos;   float _znear;
    float3   _right; float _zfar;
    float3   _up;    float _fov;
    float3   _fwd;   float _pad;
};
StructuredBuffer<CameraData> CameraBuffer : register(t0);

cbuffer SkyParams : register(b0)
{
    uint   _camIdx;
    uint   _skyHeapIdx;
    uint   _mode;          // 0=HDRI  1=FlatColor  2=Gradient
    float  _horizonWidth;  // gradient : largeur de la bande horizon [0.01, 1]
    float4 _colorSky;      // gradient : zenith ; flat : couleur unique
    float4 _colorHorizon;  // gradient : horizon
    float4 _colorGround;   // gradient : nadir
};

SamplerState      g_sampler    : register(s0);
Texture2D<float4> g_textures[] : register(t1);

static const float PI = 3.14159265358979f;

float4 main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0) : SV_Target
{
    CameraData cam = CameraBuffer[_camIdx];

    float3 dir = normalize(
        (ndc.x / cam._proj[0][0]) * cam._right +
        (ndc.y / cam._proj[1][1]) * cam._up    +
        cam._fwd);

    float3 color;

    if (_mode == 0)  // HDRI
    {
        float phi   = atan2(dir.z, dir.x);
        float theta = asin(clamp(dir.y, -1.0f, 1.0f));
        float2 uv   = float2((phi + PI) / (2.0f * PI), 0.5f - theta / PI);
        color = g_textures[_skyHeapIdx].Sample(g_sampler, uv).rgb;
    }
    else if (_mode == 1)  // FlatColor
    {
        color = _colorSky.rgb;
    }
    else  // Gradient : ciel → horizon → bas
    {
        float safeWidth  = max(_horizonWidth, 0.01f);
        float upBlend    = smoothstep(0.0f, safeWidth, dir.y);   // 0 à l'horizon, 1 au zenith
        float downBlend  = smoothstep(0.0f, safeWidth, -dir.y);  // 0 à l'horizon, 1 au nadir
        color = lerp(_colorHorizon.rgb, _colorSky.rgb,    upBlend);
        color = lerp(color,             _colorGround.rgb, downBlend);
    }

    return float4(color, 1.0f);
}
