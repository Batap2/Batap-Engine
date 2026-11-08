#include "Camera.hlsli"
#include "RtHit.hlsli"

float4 main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0) : SV_Target
{
    CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const float pixelAngle = abs(ddy(ndc.y)) / cam.proj_[1][1];

    float3 origin;
    float3 dir;
    CameraRay(cam, ndc, origin, dir);
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = cam.znear_;
    ray.TMax = RtHitFar;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(Tlas, RAY_FLAG_NONE, TlasMaskShadowCaster | TlasMaskNoShadow | TlasMaskCut, ray);
    while (query.Proceed())
    {
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    const RtSurface hit =
        RtResolveSurface(RtCommitted(query), ray.Direction, pixelAngle * query.CommittedRayT());
    if (ndc.x >= 0.0f)
        return float4(hit.surface_.N_ * 0.5f + 0.5f, 1.0f);
    if (hit.shadingModel_ == ShadingUnlit)
        return float4(hit.surface_.albedo_, 1.0f);
    float3 color = RtShadeHit(hit, -ray.Direction);
    if (g_draw.atmosphereCount_ != 0)
        color = SkyAerial(g_draw.atmosphereCount_, cam.pos_, hit.surface_.posRel_,
                          SkyFindSun(cam.pos_), color, 1.0f);
    return float4(color * FrameBuffer[0].exposure_, 1.0f);
}
