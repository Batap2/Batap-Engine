#pragma once

#include "EigenTypes.h"
#include "Renderer/GPU_GUID.h"
#include "DirtyBits.h"

#include <cstdint>
#include <span>

namespace rayvox
{
struct Camera_C
{
    GPU_GUID _buffer_ID{};
    DirtyBits _dirty{};

    v3f _pos;
    float _znear;
    v3f _forward;
    float _zfar;
    v3f _right;
    float _fov;
    m4f _view;
    m4f _proj;

    struct alignas(16) Data
    {
        v3f _pos;
        float _znear;
        v3f _forward;
        float _zfar;
        v3f _right;
        float _fov;
        m4f _view;
        m4f _proj;
    };

    std::span<const std::byte> dataView() const noexcept
    {
        auto* begin = reinterpret_cast<const std::byte*>(&_pos);
        return {begin, sizeof(Data)};
    }
};
}  // namespace rayvox