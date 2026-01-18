#pragma once

#include "EigenTypes.h"
#include "Handles.h"

#include <cstdint>
#include <span>

namespace rayvox
{
struct Camera_C
{
    bool _active = false;
    float _znear = 0;
    float _zfar = 0;
    float _fov = 0;
    m4f _view = m4f::Identity();
    m4f _proj = m4f::Identity();
};
}  // namespace rayvox
