#include "ShaderInterop.h"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::binding(BillboardsBinding, FrameSet)]]
StructuredBuffer<BillboardGPUData> Billboards;

[[vk::push_constant]] DrawPush g_draw;

struct VS_OUTPUT
{
    float4 position_ : SV_POSITION;
    float2 uv_       : TEXCOORD0;
    float4 tint_     : TEXCOORD1;
    float3 posWS_    : TEXCOORD2;
    float3 normalWS_ : TEXCOORD3;
    nointerpolation uint materialIdx_ : TEXCOORD4;
    nointerpolation uint textureIdx_  : TEXCOORD5;
};

static const float2 kCorners[6] = {
    float2(-1.f, -1.f), float2(1.f, -1.f), float2(1.f, 1.f),
    float2(-1.f, -1.f), float2(1.f, 1.f), float2(-1.f, 1.f),
};

float3 rotate(float4 q, float3 v)
{
    return v + 2.f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

VS_OUTPUT main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    BillboardGPUData b = Billboards[g_draw.instanceIndex_ + instanceId];

    float3 toCam = cam.pos_ - b.pos_;
    float  dist  = max(length(toCam), 0.0001f);

    float3 right = cam.right_;
    float3 up    = cam.up_;
    if (b.flags_ & BillboardFixed)
    {
        right = rotate(b.rot_, float3(1.f, 0.f, 0.f));
        up    = rotate(b.rot_, float3(0.f, 1.f, 0.f));
    }
    else if (b.flags_ & BillboardCylindrical)
    {
        float3 horizontal = cross(float3(0.f, 1.f, 0.f), toCam / dist);
        // Degenerate looking straight down the Y axis: keep the camera basis.
        if (dot(horizontal, horizontal) > 1e-6f)
        {
            right = normalize(horizontal);
            up    = float3(0.f, 1.f, 0.f);
        }
    }

    // Screen mode: sizes are a fraction of the viewport height, so the world
    // extent has to undo the perspective divide at this distance.
    float scale = (b.flags_ & BillboardScreenSize) ? dist * tan(cam.fov_ * 0.5f) : 0.5f;

    float2 c = kCorners[vertexId % 6u];
    float3 posWS = b.pos_ + right * (c.x * b.sizeX_ * scale) + up * (c.y * b.sizeY_ * scale);

    VS_OUTPUT o;
    o.position_ = mul(cam.proj_, mul(cam.view_, float4(posWS, 1.f)));
    o.uv_ = float2(c.x * 0.5f + 0.5f, 0.5f - c.y * 0.5f);
    o.tint_ = b.tint_;
    o.posWS_ = posWS;
    o.normalWS_ = normalize(cross(right, up));
    o.materialIdx_ = b.materialIdx_;
    o.textureIdx_ = b.textureIdx_;
    return o;
}
