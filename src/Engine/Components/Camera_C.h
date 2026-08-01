#pragma once

#include "EigenTypes.h"
#include "Reflection/ComponentRegistry.h"

namespace batap
{
struct Camera_C
{
    bool _active = false;
    float _znear = 0.1f;
    float _zfar = 1000;
    float _fov = 1;

    // aspect = width / height
    m4f make_proj(float aspect) const;
    m4f make_view(const transform& worldFromCamera) const;
};

// Fields, json keys and UI are derived from the struct. The keys must keep
// matching what scenes on disk already use — the leading '_' is stripped.
static_assert(refl::fieldName<Camera_C, 0>() == "active");
static_assert(refl::fieldName<Camera_C, 1>() == "znear");
static_assert(refl::fieldName<Camera_C, 2>() == "zfar");
static_assert(refl::fieldName<Camera_C, 3>() == "fov");

BATAP_COMPONENT(Camera_C, "camera", ComponentMeta{.flag = ComponentFlag::Camera},
                fieldMeta<&Camera_C::_znear>({.speed = 0.01f, .min = 0.001f, .max = 10.f}),
                fieldMeta<&Camera_C::_fov>({.speed = 0.01f, .min = 0.01f, .max = 3.14f}));
}  // namespace batap
