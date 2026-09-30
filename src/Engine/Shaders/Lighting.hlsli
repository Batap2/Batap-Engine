#ifndef BATAP_LIGHTING_HLSLI
#define BATAP_LIGHTING_HLSLI

#include "ShaderInterop.h"
#include "Shadows.hlsli"

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
[[vk::binding(FrameConstantsBinding, FrameSet)]]
StructuredBuffer<FrameGPUData> FrameBuffer;

[[vk::binding(SamplerBinding, BindlessSet)]]  SamplerState      g_sampler;
[[vk::binding(TexturesBinding, BindlessSet)]] Texture2D<float4> g_textures[];

[[vk::push_constant]] DrawPush g_draw;

static const float PI = 3.14159265358979f;

float3 EvalSH9(float3 N)
{
    float4 sh[9] = SkyboxBuffer[0].sh;
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

float3 SampleSky(float3 dir, float mipLevel)
{
    SkyboxGPUData sky = SkyboxBuffer[0];
    float3 result;
    if (sky.mode == 0u && sky.bindlessIndex != InvalidGPUIndex)
    {
        float  phi   = atan2(dir.z, dir.x);
        float  theta = asin(clamp(dir.y, -1.0f, 1.0f));
        float2 uv    = float2((phi + PI) / (2.0f * PI), 0.5f - theta / PI);
        result = g_textures[sky.bindlessIndex].SampleLevel(g_sampler, uv, mipLevel).rgb;
    }
    else if (sky.mode == 1u)
    {
        result = sky.color1.rgb;
    }
    else
    {
        float  hw  = max(sky.horizonWidth, 0.001f);
        float  t_u = smoothstep(0.0f, hw, dir.y);
        float  t_d = smoothstep(0.0f, hw, -dir.y);
        float3 col = lerp(sky.color2.rgb, sky.color1.rgb, t_u);
        col        = lerp(col, sky.color3.rgb, t_d);
        result = col;
    }
    return result * sky.intensity;
}

// GGX Normal Distribution Function
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d);
}

// Smith-Schlick-GGX Geometry term (single direction)
float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

// Smith combined geometry term
float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick
float3 F_Schlick(float HdotV, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - HdotV), 5.0f);
}

// The light's visibility from P, multiplying the direct term only. L points from
// P toward the light, normalized; dL is the distance it was divided by.
//
// Two methods partition the receivers and both live here, never beside it: a
// depth map where the light has one for P, analytic sphere occluders
// elsewhere. The partition is on P, not on the occluder: every mesh is drawn
// in every view, an occluder's own included, and depth clamp keeps the ones
// between the view and the light, so a P the map holds already has every
// occluder in it. Tested on the occluder instead, one inside the cascades
// would shadow nothing past them, and one outside would shadow twice the P
// inside them.
float ShadowVisibility(float3 P, float3 N, float3 L, float dL, LightGPUData light)
{
    if (light.shadowIndex_ == InvalidGPUIndex)
        return 1.0f;

    // The floor keeps smoothstep defined when sourceRadius_ is 0, where the
    // transition collapses to a hard step. A directional light carries its
    // angular radius already.
    bool directional = light.type_ == LightDirectional;
    float rL = directional ? light.sourceRadius_
                           : asin(clamp(light.sourceRadius_ / max(dL, 1e-4f), 0.0f, 1.0f));
    rL = max(rL, 1e-5f);

    // How much of P's shadow the map holds: 1 inside a cascade, a local view's
    // strength_ while it fades out with distance, 0 where there is no map.
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

    float vis = 1.0f;
    [loop]
    for (uint i = 0; i < g_draw.sphereOccluderCount_; ++i)
    {
        SphereOccluderGPUData occ = SphereOccluderBuffer[i];
        if (occ.radius_ <= 0.0f)
            continue;

        float3 S  = occ.center_ - P;
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

// ---- Rect lights: linearly transformed cosines (Heitz et al. 2016), as
// three.js writes them. A cosine lobe, transformed by a 3x3 matrix, fits the
// GGX lobe for a roughness and a view angle; its integral over a polygon has a
// closed form. The polygon goes through the inverse transform, and the result
// is the integral of the cosine lobe over it.

// The tables are indexed by (roughness, sqrt(1 - N.V)), at texel centres.
float2 LtcUv(float NdotV, float roughness)
{
    const float size = 64.0f;
    return float2(roughness, sqrt(1.0f - NdotV)) * ((size - 1.0f) / size) + 0.5f / size;
}

// Integral of the edge v1 -> v2 of a polygon projected on the unit sphere, as
// a vector: the sum over the edges is the polygon's vector form factor.
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

// The horizon clips the polygon; the table's sphere approximation does it from
// the form factor alone, which is what keeps a quad at four edges.
float LtcClippedSphere(float3 f)
{
    float l = length(f);
    return max((l * l + f.z) / (l + 1.0f), 0.0f);
}

// Integral over the rectangle of the cosine lobe transformed by mInv, in the
// frame of N and the tangent toward V. The corners wind so that their normal
// is the light's direction; from behind the light, 0.
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

// Diffuse plus specular of one rect light at P, before any shadow. radiance is
// the face's: the integrals are fractions of the hemisphere, so a Lambertian
// surface under a face that fills its sky receives the radiance times its
// albedo, and no 1/pi appears.
float3 RectLightContribution(LightGPUData light, float3 P, float3 N, float3 V, float NdotV,
                             float3 albedo, float roughness, float metallic, float3 F0)
{
    float3 toP = P - light.pos_;
    float h = dot(toP, light.direction_);
    if (h <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);

    // P in the rectangle's frame, u along its width, v along its height.
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
        return float3(0.0f, 0.0f, 0.0f);
    float d2 = d * d;
    float window = (1.0f - d2 * d2);
    window *= window;
    if (light.falloff_ > 0.0f)
        window *= pow(1.0f - d, light.falloff_);

    // Barn doors: along each axis, only the part of the face within the
    // spread of P's side emits toward P. Per axis the part is an interval, so
    // what P sees is still a rectangle and the integral stays exact.
    float2 lo = float2(-hw, -hh);
    float2 hi = float2(hw, hh);
    if (light.cosOuter_ > 0.0f)
    {
        float reach = h * sqrt(max(1.0f - light.cosOuter_ * light.cosOuter_, 0.0f)) / light.cosOuter_;
        lo = max(lo, float2(u, v) - reach);
        hi = min(hi, float2(u, v) + reach);
        if (any(hi <= lo))
            return float3(0.0f, 0.0f, 0.0f);
    }

    float3 corners[4];
    corners[0] = light.pos_ + ax * hi.x + ay * lo.y;
    corners[1] = light.pos_ + ax * lo.x + ay * lo.y;
    corners[2] = light.pos_ + ax * lo.x + ay * hi.y;
    corners[3] = light.pos_ + ax * hi.x + ay * hi.y;

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
    float3 fresnel = F0 * t2.x + (1.0f - F0) * t2.y;
    float3 specular = fresnel * LtcEvaluate(N, V, P, mInv, corners);
    float3 diffuse = albedo * (1.0f - metallic) * LtcEvaluate(N, V, P, identity, corners);

    return (diffuse + specular) * light.color_ * (light.intensity_ * window);
}

struct Surface
{
    float3 albedo_;
    float  roughness_;
    float  metallic_;
    float  reflectivity_;
    float3 N_;
    // The vertex normal, before any normal map: the shadow lookup wants the
    // receiver's geometric plane.
    float3 Ngeom_;
    float3 posWS_;
};

float3 ShadeSurface(uint shadingModel, Surface s)
{
    if (shadingModel == ShadingUnlit)
        return s.albedo_;

    CameraGPUData cam = CameraInstancebuffer[g_draw.cameraIndex_];

    // F0 : diélectrique = 0.04, métal = albedo
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), s.albedo_, s.metallic_);

    float3 V = normalize(cam.pos_ - s.posWS_);
    float  NdotV = saturate(dot(s.N_, V));

    // IBL diffuse (SH L2) + spéculaire (env sampling). The buffer keeps the last
    // sky's values after it is removed, so the count is what says it is gone.
    float3 color = float3(0.0f, 0.0f, 0.0f);
    if (g_draw.skyboxCount_ != 0)
    {
        float3 F_ibl = F_Schlick(NdotV, F0);
        float3 kD    = (1.0f - F_ibl) * (1.0f - s.metallic_);

        float3 R        = reflect(-V, s.N_);
        float  mipLevel = s.roughness_ * float(max(SkyboxBuffer[0].mipCount, 1u) - 1u);
        float3 specIBL  = F_ibl * SampleSky(R, mipLevel) * s.reflectivity_;

        color = kD * EvalSH9(s.N_) * s.albedo_ + specIBL;
    }

    uint debugCascade = ShadowCascadeCount;
    float3 debugTint = float3(0.0f, 0.0f, 0.0f);
    float debugLit = 0.0f;

    [loop]
    for (uint lightIndex = 0; lightIndex < g_draw.lightCount_; ++lightIndex)
    {
        LightGPUData light = LightBuffer[lightIndex];

        if (light.type_ == LightRect)
        {
            color += RectLightContribution(light, s.posWS_, s.N_, V, NdotV, s.albedo_,
                                           s.roughness_, s.metallic_, F0);
            continue;
        }

        // A directional light has no position and no range: radius_ is 0 for
        // it, and it reaches everything.
        float3 L;
        float  dist;
        float  rangeAtt = 1.0f;
        if (light.type_ == LightDirectional)
        {
            L = -light.direction_;
            dist = 0.0f;
        }
        else
        {
            float3 toLight = light.pos_ - s.posWS_;
            dist = length(toLight);
            if (dist > light.radius_ || dist <= 0.0001f)
                continue;
            L = toLight / dist;
            rangeAtt = pow(saturate(1.0f - dist / light.radius_), max(light.falloff_, 0.0001f));
        }

        float cone = 1.0f;
        if (light.type_ == LightSpot)
            cone = smoothstep(light.cosOuter_, light.cosInner_, dot(-L, light.direction_));
        if (cone <= 0.0f)
            continue;

        float shadow = ShadowVisibility(s.posWS_, s.Ngeom_, L, dist, light);
        if ((g_draw.debugFlags_ & DebugShadowCascades) != 0u &&
            (light.shadowIndex_ >> ShadowFamilyShift) == ShadowCascadeFamily)
        {
            debugCascade = CascadeIndexAt(s.posWS_, light.shadowIndex_ & ShadowIndexMask);
            debugTint = CascadeDebugTintAt(s.posWS_, light.shadowIndex_ & ShadowIndexMask);
            debugLit = dot(s.N_, L) > 0.0f ? shadow : 0.0f;
        }
        if (shadow <= 0.0f)
            continue;

        float3 H = normalize(V + L);

        float3 radiance = light.color_ * (rangeAtt * light.intensity_);

        float NdotL = saturate(dot(s.N_, L));
        float NdotH = saturate(dot(s.N_, H));
        float HdotV = saturate(dot(H, V));

        // Cook-Torrance specular
        float  D = D_GGX(NdotH, s.roughness_);
        float  G = G_Smith(NdotV, NdotL, s.roughness_);
        float3 F = F_Schlick(HdotV, F0);

        float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.0001f);

        // Diffuse lambertien : les métaux n'ont pas de diffuse
        float3 kD_light = (1.0f - F) * (1.0f - s.metallic_);
        float3 diffuse = kD_light * s.albedo_ / PI;

        // Direct term only: the ambient does not see shadows.
        color += (diffuse + specular) * radiance * NdotL * shadow * cone;
    }

    // Shadowed dark, lit bright, in the colour of the cascade that shaded it,
    // blended across a fade band like the shadow itself.
    if (debugCascade < ShadowCascadeCount)
        color = debugTint * lerp(0.2f, 1.0f, debugLit);

    return color;
}

#endif
