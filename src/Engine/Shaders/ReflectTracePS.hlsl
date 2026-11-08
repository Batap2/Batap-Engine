#include "Camera.hlsli"
#include "Reflections.hlsli"
#include "RtHit.hlsli"

static const float ReflectRayOffset = 1e-3f;
static const float ReflectRayOffsetPerMetre = 1e-4f;

struct TraceOutput
{
    float4 radiance_ : SV_Target0;
    float4 ray_ : SV_Target1;
};

bool ReflectTrace(float3 origin, float3 direction, out RtHitRecord record)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = RtHitFar;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(Tlas, RAY_FLAG_NONE, TlasMaskShadowCaster | TlasMaskNoShadow, ray);
    while (query.Proceed())
    {
    }
    record = (RtHitRecord)0;
    bool found = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (found)
    {
        record = RtCommitted(query);
        ray.TMax = record.t_;
    }
    if (FrameBuffer[0].rtCutInstances_ != 0u)
    {
        RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> cut;
        cut.TraceRayInline(Tlas, RAY_FLAG_NONE, TlasMaskCut, ray);
        while (cut.Proceed())
        {
            const float3 p = cut.CandidateObjectRayOrigin() +
                             cut.CandidateObjectRayDirection() * cut.CandidateTriangleRayT();
            if (MeshCutDistance(StaticMeshInstancebuffer[cut.CandidateInstanceID()], p) >= 0.0f)
                cut.CommitNonOpaqueTriangleHit();
        }
        if (cut.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            record = RtCommitted(cut);
            found = true;
        }
    }
    return found;
}

TraceOutput main(float4 svpos : SV_POSITION, float2 ndc : TEXCOORD0)
{
    const FrameGPUData frame = FrameBuffer[0];
    const CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];
    const float pixelAngle = abs(ddy(ndc.y)) / cam.proj_[1][1];
    const int3 pixel = int3(svpos.xy, 0);

    TraceOutput o;
    o.radiance_ = float4(0.0f, 0.0f, 0.0f, 0.0f);
    o.ray_ = float4(0.0f, 0.0f, 0.0f, 0.0f);
    const float depth = g_textures[frame.depthTexture_].Load(pixel).r;
    if (depth <= 0.0f)
        return o;
    const ReflectSurface surface =
        ReflectSurfaceDecode(g_uintTextures[frame.reflectSurfaceTexture_].Load(pixel).xy);
    if (surface.kind_ == ReflectKindNone || ReflectWeight(surface.roughness_) <= 0.0f)
        return o;

    float3 origin;
    float3 dir;
    CameraRay(cam, ndc, origin, dir);
    const float3 P = dir * (cam.znear_ / depth / dot(dir, cam.fwd_));
    const float3 Nview = g_textures[frame.viewNormalTexture_].Load(pixel).xyz * 2.0f - 1.0f;
    const float3 Ngeom = normalize(mul(Nview, (float3x3)cam.view_));
    const float3 V = -dir;

    const float3 L = reflect(-V, surface.N_);
    if (dot(L, Ngeom) <= 0.0f)
        return o;
    const float distance = length(P);
    const float3 start = P + Ngeom * (ReflectRayOffset + ReflectRayOffsetPerMetre * distance);

    float3 radiance = float3(0.0f, 0.0f, 0.0f);
    float hitDistance = ReflectSkyDistance;
    RtHitRecord record;
    if (ReflectTrace(start, L, record))
    {
        const RtSurface hit = RtResolveSurface(record, L, pixelAngle * (distance + record.t_));
        radiance = RtShadeHit(hit, -L);
        hitDistance = record.t_;
    }
    else if (g_draw.skyboxCount_ != 0)
    {
        radiance = SampleSky(L, 0.0f);
    }

    o.radiance_ = float4(min(radiance * frame.exposure_, ReflectMaxRadiance), max(hitDistance, 1e-4f));
    o.ray_ = float4(L, 0.0f);
    return o;
}
