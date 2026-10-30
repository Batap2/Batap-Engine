#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{

struct Atmosphere_C
{
    float planetRadius_ = 6360000.f;
    float height_ = 80000.f;
    v3f rayleigh_ = {5.8e-6f, 1.35e-5f, 3.31e-5f};
    float rayleighHeight_ = 8000.f;
    float mie_ = 2.1e-5f;
    float mieHeight_ = 1200.f;
    float mieAnisotropy_ = 0.76f;
    float boost_ = 1.5f;
    col3 groundAlbedo_ = {0.25f, 0.2f, 0.15f};
};

static_assert(refl::fieldName<Atmosphere_C, 0>() == "planetRadius");
static_assert(refl::fieldName<Atmosphere_C, 1>() == "height");
static_assert(refl::fieldName<Atmosphere_C, 2>() == "rayleigh");
static_assert(refl::fieldName<Atmosphere_C, 3>() == "rayleighHeight");
static_assert(refl::fieldName<Atmosphere_C, 4>() == "mie");
static_assert(refl::fieldName<Atmosphere_C, 5>() == "mieHeight");
static_assert(refl::fieldName<Atmosphere_C, 6>() == "mieAnisotropy");
static_assert(refl::fieldName<Atmosphere_C, 7>() == "boost");
static_assert(refl::fieldName<Atmosphere_C, 8>() == "groundAlbedo");

BATAP_COMPONENT(
    Atmosphere_C, "atmosphere", ComponentMeta{.color = ComponentColor::Cyan},
    fieldMeta<&Atmosphere_C::planetRadius_>({.speed = 1.f, .min = 0.001f, .max = 1e9f}),
    fieldMeta<&Atmosphere_C::height_>({.speed = 0.1f, .min = 0.001f, .max = 1e8f}),
    fieldMeta<&Atmosphere_C::rayleighHeight_>({.speed = 0.1f, .min = 0.001f, .max = 1e7f}),
    fieldMeta<&Atmosphere_C::mie_>({.speed = 0.0001f, .min = 0.f, .max = 10.f}),
    fieldMeta<&Atmosphere_C::mieHeight_>({.speed = 0.1f, .min = 0.001f, .max = 1e7f}),
    fieldMeta<&Atmosphere_C::mieAnisotropy_>({.speed = 0.01f, .min = -0.99f, .max = 0.99f}),
    fieldMeta<&Atmosphere_C::boost_>({.speed = 0.01f, .min = 0.f, .max = 100.f}));

}  // namespace batap
