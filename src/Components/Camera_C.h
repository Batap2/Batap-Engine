#pragma once

#include "EigenTypes.h"

namespace rayvox
{
struct Camera_C
{
    bool _active = false;
    float _znear = 0;
    float _zfar = 0;
    float _fov = 0;

    // aspect = width / height
    m4f make_proj(float aspect) const;
    m4f make_view(const transform& worldFromCamera) const;
};
}  // namespace rayvox
