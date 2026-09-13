#pragma once

#include "Assets/AssetHandle.h"
#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"
#include "Renderer/Billboards.h"

#include <cstdint>

namespace batap
{

struct Billboard_C
{
    MaterialHandle material_;
    // Overrides the material's albedo map when set.
    TextureHandle texture_;

    float width_ = 1.f;
    float height_ = 1.f;
    col3 tint_ = {1.f, 1.f, 1.f};
    float alpha_ = 1.f;

    Billboards::SizeMode sizeMode_ = Billboards::SizeMode::World;
    Billboards::Orientation orientation_ = Billboards::Orientation::Spherical;
};

static_assert(refl::fieldName<Billboard_C, 0>() == "material");
static_assert(refl::fieldName<Billboard_C, 1>() == "texture");
static_assert(refl::fieldName<Billboard_C, 2>() == "width");
static_assert(refl::fieldName<Billboard_C, 3>() == "height");
static_assert(refl::fieldName<Billboard_C, 4>() == "tint");
static_assert(refl::fieldName<Billboard_C, 5>() == "alpha");
static_assert(refl::fieldName<Billboard_C, 6>() == "sizeMode");
static_assert(refl::fieldName<Billboard_C, 7>() == "orientation");

BATAP_COMPONENT(Billboard_C, "billboard",
                fieldMeta<&Billboard_C::width_>({.speed = 0.01f, .min = 0.001f, .max = 1000.f}),
                fieldMeta<&Billboard_C::height_>({.speed = 0.01f, .min = 0.001f, .max = 1000.f}),
                fieldMeta<&Billboard_C::alpha_>({.speed = 0.01f, .min = 0.f, .max = 1.f}));

}  // namespace batap
