#pragma once

#include "Components/EntityHandle.h"
#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct World;

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

BATAP_COMPONENT(Camera_C, "camera",
                ComponentMeta{.color = ComponentColor::Blue,
                              .onDeserialized = &Camera_C::afterDeserialize},
                fieldMeta<&Camera_C::znear_>({.speed = 0.01f, .min = 0.001f, .max = 10.f}),
                fieldMeta<&Camera_C::fov_>({.speed = 0.01f, .min = 0.01f, .max = 3.14f}),
                fieldMeta<&Camera_C::ev100_>({.speed = 0.05f, .min = -10.f, .max = 25.f}),
                fieldMeta<&Camera_C::exposureCompensation_>(
                    {.speed = 0.05f, .min = -10.f, .max = 10.f}),
                fieldMeta<&Camera_C::minEv100_>({.speed = 0.05f, .min = -10.f, .max = 25.f}),
                fieldMeta<&Camera_C::maxEv100_>({.speed = 0.05f, .min = -10.f, .max = 25.f}));
}  // namespace batap
