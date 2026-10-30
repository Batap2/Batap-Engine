#include "Renderer/Atmosphere.h"

#include "Assets/AssetManager.h"
#include "Assets/Texture.h"
#include "Components/Atmosphere_C.h"
#include "Components/DirectionalLight_C.h"
#include "Components/FarPointLight_C.h"
#include "Components/Skybox_C.h"
#include "Components/Transform_C.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace batap
{

AtmosphereModel AtmosphereModel::from(const Atmosphere_C& atm, const v3f& centre)
{
    AtmosphereModel a;
    a.centre_ = centre;
    a.rayleigh_ = atm.rayleigh_;
    a.groundAlbedo_ = atm.groundAlbedo_;
    a.planetRadius_ = std::max(atm.planetRadius_, 1e-3f);
    a.atmosphereRadius_ = a.planetRadius_ + std::max(atm.height_, 1e-3f);
    a.rayleighHeight_ = std::max(atm.rayleighHeight_, 1e-3f);
    a.mie_ = atm.mie_;
    a.mieHeight_ = std::max(atm.mieHeight_, 1e-3f);
    a.mieAnisotropy_ = std::clamp(atm.mieAnisotropy_, -0.99f, 0.99f);
    a.boost_ = atm.boost_;
    return a;
}

v2f AtmosphereModel::sphereHits(const v3f& p, const v3f& d, float r) const
{
    const float b = p.dot(d);
    const v3f q = p - b * d;
    const float disc = r * r - q.squaredNorm();
    if (disc < 0.f)
        return {-1.f, -1.f};
    const float s = std::sqrt(disc);
    return {-b - s, -b + s};
}

v2f AtmosphereModel::densities(const v3f& p) const
{
    const float h = std::max(p.norm() - planetRadius_, 0.f);
    return {std::exp(-h / rayleighHeight_), std::exp(-h / mieHeight_)};
}

v2f AtmosphereModel::opticalDepth(const v3f& p, const v3f& d, int steps) const
{
    const float tEnd = sphereHits(p, d, atmosphereRadius_).y();
    if (tEnd <= 0.f || steps <= 0)
        return v2f::Zero();
    const float ds = tEnd / static_cast<float>(steps);
    v2f od = v2f::Zero();
    for (int i = 0; i < steps; ++i)
        od += densities(p + d * (ds * (static_cast<float>(i) + 0.5f))) * ds;
    return od;
}

v3f AtmosphereModel::transmittanceOf(const v2f& od) const
{
    return (-(rayleigh_ * od.x() + v3f::Constant(mie_ * 1.1f * od.y()))).array().exp();
}

v3f AtmosphereModel::transmittance(const v3f& p, const v3f& dir, int steps) const
{
    return transmittanceOf(opticalDepth(p, dir, steps));
}

v3f AtmosphereModel::radiance(const v3f& cameraPos, const v3f& dir, const SkySun& sun,
                              int viewSteps, int sunSteps) const
{
    constexpr float kPi = std::numbers::pi_v<float>;

    const v3f viewer = cameraPos - centre_;
    const v2f hits = sphereHits(viewer, dir, atmosphereRadius_);
    if (hits.y() <= 0.f || viewSteps <= 0 || !sun.valid_)
        return v3f::Zero();
    const v2f ground = sphereHits(viewer, dir, planetRadius_);
    const bool hitsGround = ground.x() > 0.f;
    const float tStart = std::max(hits.x(), 0.f);
    const float tEnd = hitsGround ? ground.x() : hits.y();
    if (tEnd <= tStart)
        return v3f::Zero();

    const float mu = dir.dot(sun.dir_);
    const float g = mieAnisotropy_;
    const float phaseR = 3.f / (16.f * kPi) * (1.f + mu * mu);
    const float phaseM = 3.f / (8.f * kPi) * (1.f - g * g) * (1.f + mu * mu) /
                         ((2.f + g * g) * std::pow(1.f + g * g - 2.f * g * mu, 1.5f));

    const float ds = (tEnd - tStart) / static_cast<float>(viewSteps);
    v3f sumR = v3f::Zero();
    v3f sumM = v3f::Zero();
    v2f od = v2f::Zero();
    for (int i = 0; i < viewSteps; ++i)
    {
        const v3f p = viewer + dir * (tStart + ds * (static_cast<float>(i) + 0.5f));
        const v2f dens = densities(p) * ds;
        od += dens;
        const v3f att = transmittanceOf(od + opticalDepth(p, sun.dir_, sunSteps));
        sumR += att * dens.x();
        sumM += att * dens.y();
    }
    const float strength = sun.intensity_ * boost_;
    v3f result = strength * (sumR.cwiseProduct(rayleigh_) * phaseR + sumM * (mie_ * phaseM));
    if (hitsGround)
    {
        const v3f pg = viewer + dir * tEnd;
        const float lit = std::max(pg.normalized().dot(sun.dir_), 0.f);
        const v3f sunT = transmittance(pg, sun.dir_, sunSteps);
        result += groundAlbedo_.cwiseProduct(sunT).cwiseProduct(transmittanceOf(od)) *
                  (strength / kPi * lit);
    }
    return result;
}

SkySun skySunOf(const entt::registry& reg, const v3f& cameraPos)
{
    SkySun sun;
    for (auto [e, light, tc] : reg.view<const FarPointLight_C, const Transform_C>().each())
    {
        const v3f toSun = tc.world().translation() - cameraPos;
        if (toSun.squaredNorm() < 1e-6f)
            continue;
        sun.dir_ = toSun.normalized();
        sun.intensity_ = light.intensity_;
        sun.valid_ = true;
        return sun;
    }
    const auto suns = reg.view<const DirectionalLight_C, const Transform_C>();
    if (suns.begin() != suns.end())
    {
        const entt::entity e = *suns.begin();
        sun.dir_ = suns.get<const Transform_C>(e).world().linear().col(2).normalized();
        sun.intensity_ = suns.get<const DirectionalLight_C>(e).intensity_;
        sun.valid_ = true;
    }
    return sun;
}

std::vector<AtmosphereModel> atmospheresOf(const entt::registry& reg)
{
    std::vector<AtmosphereModel> out;
    for (auto [e, atm, tc] : reg.view<const Atmosphere_C, const Transform_C>().each())
        out.push_back(AtmosphereModel::from(atm, tc.world().translation()));
    return out;
}

SH9 skyIrradianceAt(const entt::registry& reg, AssetManager& assets, const v3f& cameraPos)
{
    SH9 sh;
    const auto skies = reg.view<const Skybox_C>();
    if (skies.begin() != skies.end())
    {
        const Skybox_C& sky = skies.get<const Skybox_C>(*skies.begin());
        SH9 base;
        if (sky.mode_ == Skybox_C::Mode::HDRI)
        {
            if (sky.hdri_)
                if (const Texture* tex = assets.get<Texture>(sky.hdri_))
                    base = tex->irradianceSH_;
        }
        else
            base = projectSkyToSH(sky);
        for (size_t i = 0; i < sh.c.size(); ++i)
            sh.c[i] = base.c[i] * sky.intensity_;
    }

    const std::vector<AtmosphereModel> atmospheres = atmospheresOf(reg);
    if (atmospheres.empty())
        return sh;
    const SkySun sun = skySunOf(reg, cameraPos);
    if (!sun.valid_)
        return sh;

    constexpr size_t N = 256;
    constexpr float kGoldenAngle = std::numbers::pi_v<float> * (3.f - 1.6180339887f * 2.f);
    constexpr float kWeight = 4.f * std::numbers::pi_v<float> / static_cast<float>(N);
    for (size_t i = 0; i < N; ++i)
    {
        const float fi = static_cast<float>(i);
        const float yi = 1.f - (2.f * fi + 1.f) / static_cast<float>(N);
        const float r = std::sqrt(std::max(0.f, 1.f - yi * yi));
        const float theta = kGoldenAngle * fi;
        const v3f dir{std::cos(theta) * r, yi, std::sin(theta) * r};
        v3f color = v3f::Zero();
        for (const AtmosphereModel& atm : atmospheres)
            color += atm.radiance(cameraPos, dir, sun, 8, 4);
        accumSH(sh, color, dir.x(), dir.y(), dir.z(), kWeight);
    }
    return sh;
}

}  // namespace batap
