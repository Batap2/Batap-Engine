#pragma once

#include "DirtyBits.h"
#include "EigenTypes.h"
#include "Handles.h"

#include <cstdint>
#include <span>

namespace rayvox
{
struct Camera_C
{
    GPUHandle _buffer_ID;
    DirtyBits _dirty{};

    struct alignas(16) Data
    {
        v3f _pos = v3f(0, 0, 0);
        float _znear = 0;
        v3f _forward = v3f(0, 0, 0);
        float _zfar = 0;
        v3f _right = v3f(0, 0, 0);
        float _fov = 0;
        m4f _view = m4f::Identity();
        m4f _proj = m4f::Identity();
    } _data{};

    v3f& _pos() { return _data._pos; }
    const v3f& _pos() const { return _data._pos; }

    float& _znear() { return _data._znear; }
    const float& _znear() const { return _data._znear; }

    v3f& _forward() { return _data._forward; }
    const v3f& _forward() const { return _data._forward; }

    float& _zfar() { return _data._zfar; }
    const float& _zfar() const { return _data._zfar; }

    v3f& _right() { return _data._right; }
    const v3f& _right() const { return _data._right; }

    float& _fov() { return _data._fov; }
    const float& _fov() const { return _data._fov; }

    m4f& _view() { return _data._view; }
    const m4f& _view() const { return _data._view; }

    m4f& _proj() { return _data._proj; }
    const m4f& _proj() const { return _data._proj; }
};
}  // namespace rayvox