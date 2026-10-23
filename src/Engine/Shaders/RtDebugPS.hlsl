#include "RtHit.hlsli"

float4 main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0) : SV_Target
{
    CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];

    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, 0.0f);
    ray.Direction = normalize((ndc.x / cam.proj_[0][0]) * cam.right_ +
                              (ndc.y / cam.proj_[1][1]) * cam.up_ + cam.fwd_);
    ray.TMin = cam.znear_;
    ray.TMax = RtHitFar;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(Tlas, RAY_FLAG_NONE, TlasMaskShadowCaster | TlasMaskNoShadow, ray);
    while (query.Proceed())
    {
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    const RtHit hit = RtResolveHit(query, ray.Direction);
    return ndc.x < 0.0f ? float4(hit.albedo_, 1.0f) : float4(hit.normal_ * 0.5f + 0.5f, 1.0f);
}
