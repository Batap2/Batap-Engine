#pragma once

#include "Components/EntityHandle.h"
#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct World;

inline constexpr float kOrthoDepth = 10000.f;

struct Camera_C
{
    static void afterDeserialize(EntityHandle h, World& world);

    bool active_ = false;
    float znear_ = 0.1f;
    float fov_ = 1;
    float ev100_ = 15;
    bool autoExposure_ = false;
    float exposureCompensation_ = 0;
    float minEv100_ = -6;
    float maxEv100_ = 20;
    float meteringPercentile_ = 0.98f;
    float meteringRange_ = 6;
    float orthoHeight_ = 0;
    float bloomIntensity_ = 0.1f;
    float bloomRadius_ = 1;
    float aoStrength_ = 1;
    // Raster only: in ray tracing the radius is the GI cache cell.
    float aoRadius_ = 1;
    float aoThickness_ = 0.1f;

    bool ortho() const { return orthoHeight_ > 0; }

    // aspect = width / height
    m4f make_proj(float aspect) const;
    m4f make_view(const transform& worldFromCamera) const;
};

float exposureForEv100(float ev100);
float ev100ForExposure(float exposure);

// Fields, json keys and UI are derived from the struct. The keys must keep
// matching what scenes on disk already use — the leading '_' is stripped.
static_assert(refl::fieldName<Camera_C, 0>() == "active");
static_assert(refl::fieldName<Camera_C, 1>() == "znear");
static_assert(refl::fieldName<Camera_C, 2>() == "fov");

BATAP_COMPONENT(
    Camera_C, "camera",
    ComponentMeta{.color = ComponentColor::Blue, .onDeserialized = &Camera_C::afterDeserialize},
    fieldMeta<&Camera_C::znear_>({.speed = 0.01f, .min = 0.001f, .max = 10.f}),
    fieldMeta<&Camera_C::fov_>({.speed = 0.01f, .min = 0.01f, .max = 3.14f}),
    fieldMeta<&Camera_C::ev100_>({.speed = 0.05f, .min = -10.f, .max = 25.f}),
    fieldMeta<&Camera_C::exposureCompensation_>({.speed = 0.05f, .min = -10.f, .max = 10.f}),
    fieldMeta<&Camera_C::minEv100_>({.speed = 0.05f, .min = -10.f, .max = 25.f}),
    fieldMeta<&Camera_C::maxEv100_>({.speed = 0.05f, .min = -10.f, .max = 25.f}),
    fieldMeta<&Camera_C::meteringPercentile_>({.speed = 0.005f, .min = 0.5f, .max = 1.f}),
    fieldMeta<&Camera_C::meteringRange_>({.speed = 0.05f, .min = 0.f, .max = 20.f}),
    fieldSkip<&Camera_C::orthoHeight_>(),
    fieldMeta<&Camera_C::bloomIntensity_>({.speed = 0.002f, .min = 0.f, .max = 1.f}),
    fieldMeta<&Camera_C::bloomRadius_>({.speed = 0.01f, .min = 0.25f, .max = 4.f}),
    fieldMeta<&Camera_C::aoStrength_>({.speed = 0.01f, .min = 0.f, .max = 1.f}),
    fieldMeta<&Camera_C::aoRadius_>({.speed = 0.01f, .min = 0.05f, .max = 10.f}),
    fieldMeta<&Camera_C::aoThickness_>({.speed = 0.005f, .min = 0.f, .max = 2.f}));
}  // namespace batap
