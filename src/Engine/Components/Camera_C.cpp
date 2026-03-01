#include "Camera_C.h"

namespace batap
{
m4f Camera_C::make_proj(float aspect) const
{
    if (aspect <= 0.0f || _znear <= 0.0f || _zfar <= _znear || _fov <= 0.0f)
        return m4f::Identity();

    const float f = 1.0f / std::tan(_fov * 0.5f);
    const float nf = 1.0f / (_znear - _zfar);

    m4f m = m4f::Zero();

    // RH coordinate system, forward is -Z
    // NDC z ∈ [0, 1] (D3D/Vulkan style)
    m(0, 0) = f / aspect;
    m(1, 1) = f;

    m(2, 2) = _zfar * nf;
    m(2, 3) = (_zfar * _znear) * nf;
    m(3, 2) = -1.0f;

    return m;
}

m4f Camera_C::make_view(const transform& worldFromCamera) const
{
    return worldFromCamera.inverse().matrix();
}
}  // namespace batap
