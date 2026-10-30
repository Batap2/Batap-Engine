#pragma once

#include "EigenTypes.h"
#include "Renderer/SkyIrradiance.h"

#include <entt/entt.hpp>

#include <vector>

namespace batap
{
struct Atmosphere_C;
struct AssetManager;

struct SkySun
{
    v3f dir_ = v3f::UnitY();
    float intensity_ = 0.f;
    bool valid_ = false;
};

struct AtmosphereModel
{
    static AtmosphereModel from(const Atmosphere_C& atm, const v3f& centre);

    v3f radiance(const v3f& cameraPos, const v3f& dir, const SkySun& sun, int viewSteps,
                 int sunSteps) const;
    v3f transmittance(const v3f& p, const v3f& dir, int steps) const;

    v3f centre_ = v3f::Zero();
    v3f rayleigh_ = v3f::Zero();
    v3f groundAlbedo_ = v3f::Zero();
    float planetRadius_ = 1.f;
    float atmosphereRadius_ = 1.f;
    float rayleighHeight_ = 1.f;
    float mie_ = 0.f;
    float mieHeight_ = 1.f;
    float mieAnisotropy_ = 0.f;
    float boost_ = 1.f;

   private:
    v2f sphereHits(const v3f& p, const v3f& d, float r) const;
    v2f densities(const v3f& p) const;
    v2f opticalDepth(const v3f& p, const v3f& d, int steps) const;
    v3f transmittanceOf(const v2f& od) const;
};

SkySun skySunOf(const entt::registry& reg, const v3f& cameraPos);
std::vector<AtmosphereModel> atmospheresOf(const entt::registry& reg);
SH9 skyIrradianceAt(const entt::registry& reg, AssetManager& assets, const v3f& cameraPos);

}  // namespace batap
