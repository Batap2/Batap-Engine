#pragma once

#include "EigenTypes.h"

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
}  // namespace batap
