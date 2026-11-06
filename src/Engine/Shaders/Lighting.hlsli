#ifndef BATAP_LIGHTING_HLSLI
#define BATAP_LIGHTING_HLSLI

#include "Camera.hlsli"
#include "GiCache.hlsli"
#include "Glints.hlsli"
#include "ShaderInterop.h"
#include "Shadows.hlsli"
// The in-scatter on surfaces sees the relief's shadow through the sun's cascades.
#define SKY_AIR_VISIBILITY(pRel, sun) ShadowAirVisibility(pRel, sun.dir, sun.shadowIndex)
#include "Sky.hlsli"

[[vk::binding(CamerasBinding, FrameSet)]]
StructuredBuffer<CameraGPUData> CameraInstancebuffer;
[[vk::binding(LightsBinding, FrameSet)]]
StructuredBuffer<LightGPUData> LightBuffer;
[[vk::binding(MaterialsBinding, FrameSet)]]
StructuredBuffer<Material> MaterialBuffer;
[[vk::binding(SkyboxBinding, FrameSet)]]
StructuredBuffer<SkyboxGPUData> SkyboxBuffer;
[[vk::binding(SphereOccludersBinding, FrameSet)]]
StructuredBuffer<SphereOccluderGPUData> SphereOccluderBuffer;

[[vk::binding(SamplerBinding, BindlessSet)]]  SamplerState      g_sampler;
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];

[[vk::push_constant]] DrawPush g_draw;

static const float PI = 3.14159265358979f;

float3 EvalSH9(float3 N)
{
    float4 sh[9];
    if (g_draw.atmosphereCount_ != 0)
        sh = FrameBuffer[0].sh_;
    else
        sh = SkyboxBuffer[0].sh;
    return max(0.0f,
          sh[0].rgb * 0.886227f
        + sh[1].rgb * (1.023327f * N.y)
        + sh[2].rgb * (1.023327f * N.z)
        + sh[3].rgb * (1.023327f * N.x)
        + sh[4].rgb * (0.858086f * N.x * N.y)
        + sh[5].rgb * (0.858086f * N.y * N.z)
        + sh[6].rgb * (0.743125f * N.y * N.y - 0.247708f)
        + sh[7].rgb * (0.858086f * N.x * N.z)
        + sh[8].rgb * (0.429043f * (N.x * N.x - N.z * N.z)));
}

SkySun SkyFindSun(float3 cameraPos)
{
    SkySun sun = SkyNoSun();
    for (uint i = 0; i < g_draw.lightCount_ && !sun.valid; ++i)
        sun = SkySunOf(LightBuffer[i], cameraPos);
    return sun;
}

float3 SampleSky(float3 dir, float mipLevel)
{
    SkyboxGPUData sky = SkyboxBuffer[0];
    float3 background;
    if (sky.mode == 0u && sky.bindlessIndex != InvalidGPUIndex)
    {
        background = g_textures[sky.bindlessIndex]
                         .SampleLevel(g_sampler, SkyEquirectUv(dir), mipLevel)
                         .rgb;
    }
    else
    {
        background = SkyBackground(sky, dir);
    }
    background *= sky.intensity;
    // No sun disc: the lights carry the sun, and a reflection or a GI ray
    // that caught it would count it twice.
    float3 cameraPos = FrameBuffer[0].cameraPos_;
    return SkyComposite(g_draw.atmosphereCount_, cameraPos, dir, SkyFindSun(cameraPos), background,
                        false);
}

float3 SampleSkyPrefiltered(float3 dir, float roughness)
{
    FrameGPUData frame = FrameBuffer[0];
    float level = roughness * float(SkyPrefilterLevels - 1u);
    uint i0 = uint(level);
    uint i1 = min(i0 + 1u, SkyPrefilterLevels - 1u);
    float2 uv = SkyEquirectUv(dir);
    float3 a = g_textures[NonUniformResourceIndex(frame.skyLevels_[i0])]
                   .SampleLevel(g_sampler, uv, 0.0f)
                   .rgb;
    float3 b = g_textures[NonUniformResourceIndex(frame.skyLevels_[i1])]
                   .SampleLevel(g_sampler, uv, 0.0f)
                   .rgb;
    return lerp(a, b, frac(level));
}

// GGX Normal Distribution Function
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d);
}

// Height-correlated Smith visibility for GGX (Heitz 2014), with the
// 1 / (4 NdotV NdotL) of Cook-Torrance folded in: specular = D * Vis * F.
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    return 0.5f / max(gv + gl, 1e-5f);
}

// Fresnel-Schlick
float3 F_Schlick(float HdotV, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - HdotV), 5.0f);
}

// A depth map where the light has one for P, analytic sphere occluders
// elsewhere. The partition is on P, not on the occluder: every mesh is drawn
// in every view and depth clamp keeps the casters between the view and the
// light, so a P the map holds already has every occluder in it.
float ShadowVisibility(float3 P, float3 N, float3 Nface, float3 L, float dL, LightGPUData light,
                       float frontOnlyUntil = 0.0f)
{
    if (light.shadowIndex_ == InvalidGPUIndex)
        return 1.0f;

#if BATAP_RT
    if (light.type_ != LightRect)
        return TraceShadowRay(P, Nface, L, light.type_ == LightDirectional ? ShadowRayFar : dL,
                              frontOnlyUntil);
#endif

    // The floor keeps smoothstep defined when sourceRadius_ is 0, where the
    // transition collapses to a hard step.
    bool directional = light.type_ == LightDirectional;
    float rL = directional ? light.sourceRadius_
                           : asin(clamp(light.sourceRadius_ / max(dL, 1e-4f), 0.0f, 1.0f));
    rL = max(rL, 1e-5f);

    float coverage = 0.0f;
    float mapVis = 1.0f;

    uint family = light.shadowIndex_ >> ShadowFamilyShift;
    uint firstView = light.shadowIndex_ & ShadowIndexMask;
    if (family == ShadowLocalSingle)
        mapVis = ShadowMapVisibility(P, N, L, dL, firstView, coverage);
    else if (family == ShadowLocalCube)
        mapVis = ShadowMapVisibility(P, N, L, dL, firstView + CubeFaceIndex(-L), coverage);
    else if (family == ShadowCascadeFamily)
        mapVis = CascadeVisibility(P, N, L, directional ? 1.0f : dL, firstView, coverage);

    if (coverage >= 1.0f)
        return mapVis;

    float3 camPos = CameraInstancebuffer[g_draw.cameraIndex_].pos_;
    float vis = 1.0f;
    [loop]
    for (uint i = 0; i < g_draw.sphereOccluderCount_; ++i)
    {
        SphereOccluderGPUData occ = SphereOccluderBuffer[i];
        if (occ.radius_ <= 0.0f)
            continue;

        float3 S  = (occ.center_ - camPos) - P;
        float  dS = length(S);
        if (dS <= occ.radius_)  // inside the occluder: its own relief must not
            continue;           // put it out
        S /= dS;

        float cosSep = dot(L, S);
        if (cosSep <= 0.0f)     // occluder behind the point: nothing
            continue;

        // Overlap of two discs on the sphere of directions.
        float sep = acos(clamp(cosSep, -1.0f, 1.0f));
        float rO  = asin(clamp(occ.radius_ / dS, 0.0f, 1.0f));

        // Independent by assumption: two occluders overlapping on the source's
        // disc over-darken. Invisible on a double transit, wrong elsewhere.
        vis *= smoothstep(-rL, rL, sep - rO);
    }

    // A local map fading out hands its receivers over to the occluders.
    return lerp(vis, mapVis, coverage);
}

// Rect lights: linearly transformed cosines (Heitz et al. 2016), as three.js
// writes them.

float2 LtcUv(float NdotV, float roughness)
{
    const float size = 64.0f;
    return float2(roughness, sqrt(1.0f - NdotV)) * ((size - 1.0f) / size) + 0.5f / size;
}

// The LTC amplitude table doubles as the DFG lookup: x is the GGX albedo at
// F0 = 1, y its F90 part (checked by Compacteur's tools/check_ltc_dfg.py).
float2 Dfg(float NdotV, float roughness)
{
    FrameGPUData frame = FrameBuffer[0];
    return g_textures[frame.ltcAmpTexture_]
        .SampleLevel(g_sampler, LtcUv(NdotV, roughness), 0.0f)
        .xy;
}

// Turquin 2019: what the single-scattering lobe loses comes back, scaled by F0.
float3 SpecularEnergy(float3 F0, float albedoAtOne)
{
    return 1.0f + F0 * (1.0f / max(albedoAtOne, 1e-3f) - 1.0f);
}

// Rational fit of theta / sin(theta), accurate to float and free of acos.
float3 LtcEdgeFormFactor(float3 v1, float3 v2)
{
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985f + (0.4965155f + 0.0145206f * y) * y;
    float b = 3.4175940f + (4.1616724f + y) * y;
    float v = a / b;
    float thetaSinTheta = x > 0.0f ? v : 0.5f * rsqrt(max(1.0f - x * x, 1e-7f)) - v;
    return cross(v1, v2) * thetaSinTheta;
}

// Horizon clipping from the form factor alone, via the sphere approximation:
// it keeps a quad at four edges.
float LtcClippedSphere(float3 f)
{
    float l = length(f);
    return max((l * l + f.z) / (l + 1.0f), 0.0f);
}

// The corners must wind so that their normal is the light's direction.
float LtcEvaluate(float3 N, float3 V, float3 P, float3x3 mInv, float3 corners[4])
{
    float3 T1 = normalize(V - N * dot(V, N));
    float3 T2 = -cross(N, T1);
    float3x3 m = mul(mInv, float3x3(T1, T2, N));

    float3 c0 = normalize(mul(m, corners[0] - P));
    float3 c1 = normalize(mul(m, corners[1] - P));
    float3 c2 = normalize(mul(m, corners[2] - P));
    float3 c3 = normalize(mul(m, corners[3] - P));

    float3 f = LtcEdgeFormFactor(c0, c1) + LtcEdgeFormFactor(c1, c2) +
               LtcEdgeFormFactor(c2, c3) + LtcEdgeFormFactor(c3, c0);
    return LtcClippedSphere(f);
}

struct RectFace
{
    float3 corners_[4];
    float window_;
};

RectFace RectLightFace(LightGPUData light, float3 P)
{
    RectFace face = (RectFace)0;
    float3 toP = P - light.pos_;
    float h = dot(toP, light.direction_);
    if (h <= 0.0f)
        return face;

    float hw = length(light.halfWidth_);
    float hh = length(light.halfHeight_);
    float3 ax = light.halfWidth_ / max(hw, 1e-6f);
    float3 ay = light.halfHeight_ / max(hh, 1e-6f);
    float u = dot(toP, ax);
    float v = dot(toP, ay);

    // Range window on the distance to the rectangle itself, not its centre: a
    // long tube light reaches as far from its ends as from its middle.
    float2 outside = max(abs(float2(u, v)) - float2(hw, hh), 0.0f);
    float d = sqrt(dot(outside, outside) + h * h) / max(light.radius_, 1e-4f);
    if (d >= 1.0f)
        return face;
    float d2 = d * d;
    float window = (1.0f - d2 * d2);
    window *= window;
    if (light.falloff_ > 0.0f)
        window *= pow(1.0f - d, light.falloff_);

    // Barn doors: per axis only an interval of the face emits toward P, so
    // what P sees is still a rectangle and the integral stays exact.
    float2 lo = float2(-hw, -hh);
    float2 hi = float2(hw, hh);
    if (light.cosOuter_ > 0.0f)
    {
        float reach = h * sqrt(max(1.0f - light.cosOuter_ * light.cosOuter_, 0.0f)) / light.cosOuter_;
        lo = max(lo, float2(u, v) - reach);
        hi = min(hi, float2(u, v) + reach);
        if (any(hi <= lo))
            return face;
    }

    face.corners_[0] = light.pos_ + ax * hi.x + ay * lo.y;
    face.corners_[1] = light.pos_ + ax * lo.x + ay * lo.y;
    face.corners_[2] = light.pos_ + ax * lo.x + ay * hi.y;
    face.corners_[3] = light.pos_ + ax * hi.x + ay * hi.y;
    face.window_ = window;
    return face;
}

// intensity_ is the face's radiance: the integrals are fractions of the
// hemisphere, so no 1/pi appears.
float3 RectLightContribution(LightGPUData light, float3 P, float3 N, float3 V, float NdotV,
                             float3 albedo, float roughness, float metallic, float3 F0,
                             float coverage = 1.0f)
{
    const RectFace face = RectLightFace(light, P);
    if (face.window_ <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);

    FrameGPUData frame = FrameBuffer[0];
    float2 uv = LtcUv(NdotV, roughness);
    float4 t1 = g_textures[frame.ltcMatTexture_].SampleLevel(g_sampler, uv, 0.0f);
    float4 t2 = g_textures[frame.ltcAmpTexture_].SampleLevel(g_sampler, uv, 0.0f);
    float3x3 mInv = float3x3(t1.x, 0.0f, t1.z,
                             0.0f, 1.0f, 0.0f,
                             t1.y, 0.0f, t1.w);
    float3x3 identity = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);

    // Hill's split of the Fresnel term over the lobe: F0 scaled by the GGX
    // albedo, and the rest toward F90 = 1.
    float3 lobe = (F0 * t2.x + (1.0f - F0) * t2.y) * SpecularEnergy(F0, t2.x);
    float3 specular = lobe * LtcEvaluate(N, V, P, mInv, face.corners_);
    float3 diffuse = albedo * (1.0f - lobe) * (1.0f - metallic) *
                     LtcEvaluate(N, V, P, identity, face.corners_) * coverage;

    return (diffuse + specular) * light.color_ * (light.intensity_ * face.window_);
}

// Read from where the maps were drawn: the apex of the doors' pyramid for one
// view, the face's centre for a cube.
float RectLightShadow(LightGPUData light, float3 P, float3 Ngeom, float3 Nface)
{
    float3 origin = light.pos_;
    if ((light.shadowIndex_ >> ShadowFamilyShift) == ShadowLocalSingle)
    {
        float tanHalf = sqrt(max(1.0f - light.cosOuter_ * light.cosOuter_, 0.0f)) /
                        max(light.cosOuter_, 1e-4f);
        float half = max(length(light.halfWidth_), length(light.halfHeight_));
        origin -= light.direction_ * (half / max(tanHalf, 1e-4f));
    }
    float3 toOrigin = origin - P;
    float dL = length(toOrigin);
    return ShadowVisibility(P, Ngeom, Nface, toOrigin / max(dL, 1e-6f), dL, light);
}

struct Surface
{
    float3 albedo_;
    float  roughness_;
    float  metallic_;
    float  ior_;
    float3 emissive_;
    float3 N_;
    float3 Ngeom_;
    float3 Nface_;
    // World axes, camera at the origin: the TLAS space, where rays start.
    float3 posRel_;
    float cell_;
    GiPoint gi_;
    float2 uv_;
    float2 uvDx_;
    float2 uvDy_;
    float3 T_;
    float glintDensity_;
    float glintIntensity_;
};

static const float FacetEscapeCells = 2.0f;

float3 FaceNormal(float3 P, float3 Ngeom)
{
    float3 c = cross(ddx(P), ddy(P));
    float l2 = dot(c, c);
    if (l2 <= 1e-30f)
        return Ngeom;
    c *= rsqrt(l2);
    return dot(c, Ngeom) >= 0.0f ? c : -c;
}

// The sky buffer keeps the last sky's values after it is removed, so the
// count is what says it is gone.
float3 DiffuseIrradiance(Surface s)
{
    const float3 sky = g_draw.skyboxCount_ != 0 ? EvalSH9(s.N_) : float3(0.0f, 0.0f, 0.0f);
#if BATAP_RT
    return GiIrradianceAt(s.gi_, length(s.posRel_), sky);
#else
    return sky;
#endif
}

bool LightIncidence(LightGPUData light, float3 P, out float3 L, out float dist,
                    out float attenuation)
{
    L = -light.direction_;
    dist = 0.0f;
    attenuation = 1.0f;
    if (light.type_ != LightDirectional)
    {
        float3 toLight = light.pos_ - P;
        dist = length(toLight);
        if (dist > light.radius_ || dist <= 0.0001f)
            return false;
        L = toLight / dist;
        const float d = dist / light.radius_;
        const float d2 = d * d;
        attenuation = saturate(1.0f - d2 * d2);
        attenuation *= attenuation;
        if (light.falloff_ > 0.0f)
            attenuation *= pow(1.0f - d, light.falloff_);
        if (light.type_ != LightFarPoint)
            attenuation /= max(dist * dist, max(light.sourceRadius_ * light.sourceRadius_, 1e-4f));
    }
    if (light.type_ == LightSpot)
        attenuation *= smoothstep(light.cosOuter_, light.cosInner_, dot(-L, light.direction_));
    return attenuation > 0.0f;
}

float3 ShadeSurface(uint shadingModel, Surface s, float coverage = 1.0f)
{
    if (shadingModel == ShadingUnlit)
        return s.albedo_ * coverage;

    CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];

    float f0 = (s.ior_ - 1.0f) / (s.ior_ + 1.0f);
    float3 F0 = lerp((f0 * f0).xxx, s.albedo_, s.metallic_);

    float3 V = CameraToEye(cam, s.posRel_);
    float  NdotV = saturate(dot(s.N_, V));
    float2 dfg = Dfg(NdotV, s.roughness_);
    float3 energy = SpecularEnergy(F0, dfg.x);
    float3 lobe = (F0 * (dfg.x - dfg.y) + dfg.y) * energy;
    // What the whole specular lobe sends back is what the diffuse never sees.
    float3 kD = (1.0f - lobe) * (1.0f - s.metallic_);

    float3 color = kD * DiffuseIrradiance(s) * s.albedo_ / PI * coverage;
    if (g_draw.skyboxCount_ != 0)
        color += SampleSkyPrefiltered(reflect(-V, s.N_), s.roughness_) * lobe;

    uint debugCascade = ShadowCascadeCount;
    float3 debugTint = float3(0.0f, 0.0f, 0.0f);
    float debugLit = 0.0f;

    [loop]
    for (uint lightIndex = 0; lightIndex < g_draw.lightCount_; ++lightIndex)
    {
        LightGPUData light = LightBuffer[lightIndex];
        light.pos_ -= cam.pos_;

        if (light.type_ == LightRect)
        {
            float3 lit = RectLightContribution(light, s.posRel_, s.N_, V, NdotV, s.albedo_,
                                               s.roughness_, s.metallic_, F0, coverage);
            if (any(lit > 0.0f))
                lit *= RectLightShadow(light, s.posRel_, s.Ngeom_, s.Nface_);
            color += lit;
            continue;
        }

        float3 L;
        float  dist;
        float  attenuation;
        if (!LightIncidence(light, s.posRel_, L, dist, attenuation))
            continue;

        float NdotL = saturate(dot(s.N_, L));
        float shadow =
            NdotL > 0.0f ? ShadowVisibility(s.posRel_, s.Ngeom_, s.Nface_, L, dist, light,
                                            FacetEscapeCells * s.cell_)
                         : 0.0f;
        if ((g_draw.debugFlags_ & DebugShadowCascades) != 0u &&
            (light.shadowIndex_ >> ShadowFamilyShift) == ShadowCascadeFamily)
        {
            debugCascade = CascadeIndexAt(s.posRel_, light.shadowIndex_ & ShadowIndexMask);
            debugTint = CascadeDebugTintAt(s.posRel_, light.shadowIndex_ & ShadowIndexMask);
            debugLit = shadow;
        }
        if (shadow <= 0.0f)
            continue;

        float3 H = normalize(V + L);

        float3 radiance = light.color_ * (attenuation * light.intensity_);
        if ((light.type_ == LightFarPoint || light.type_ == LightDirectional) &&
            g_draw.atmosphereCount_ != 0)
            radiance *= SkySunTransmittance(g_draw.atmosphereCount_, cam.pos_ + s.posRel_, L);

        float NdotH = saturate(dot(s.N_, H));
        float HdotV = saturate(dot(H, V));

        // Cook-Torrance specular
        float  D = D_GGX(NdotH, s.roughness_);
        if (s.glintDensity_ > 0.0f)
            D *= (1.0f - min(s.glintIntensity_, 1.0f)) +
                 s.glintIntensity_ * GlintRatio(s.uv_, s.uvDx_, s.uvDy_, s.T_, s.N_, H,
                                                s.roughness_ * s.roughness_, s.glintDensity_);
        float  Vis = V_SmithGGXCorrelated(NdotV, NdotL, s.roughness_);
        float3 F = F_Schlick(HdotV, F0);

        float3 specular = D * Vis * F * energy;

        float3 diffuse = kD * s.albedo_ / PI * coverage;

        // Direct term only: the ambient does not see shadows.
        color += (diffuse + specular) * radiance * NdotL * shadow;
    }

    color += s.emissive_;
    if (g_draw.atmosphereCount_ != 0)
        color = SkyAerial(g_draw.atmosphereCount_, cam.pos_, s.posRel_, SkyFindSun(cam.pos_), color,
                          coverage);

    color *= FrameBuffer[0].exposure_;
    if (debugCascade < ShadowCascadeCount)
        color = debugTint * lerp(0.2f, 1.0f, debugLit);
    if ((g_draw.debugFlags_ & DebugGiCells) != 0u)
        color = GiDebugColor(GiCacheKey(s.gi_, GiLevel(length(s.posRel_))));
    if ((g_draw.debugFlags_ & DebugGiSides) != 0u)
        color = GiSideColor(GiSide(s.gi_));
#if BATAP_RT
    if ((g_draw.debugFlags_ & DebugGiCache) != 0u)
    {
        const uint slot = GiSlotFind(GiCacheKey(s.gi_, GiLevel(length(s.posRel_))));
        color = slot == GiNoSlot ? float3(0.5f, 0.0f, 0.5f)
                                 : GiIrradiance(slot) * (FrameBuffer[0].exposure_ / PI);
    }
#endif

    return color;
}

#endif
