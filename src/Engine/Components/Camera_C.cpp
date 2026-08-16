#include "Camera_C.h"

namespace batap
{
m4f Camera_C::make_proj(float aspect) const
{
    if (aspect <= 0.0f || znear_ <= 0.0f || zfar_ <= znear_ || fov_ <= 0.0f)
        return m4f::Identity();

    const float f = 1.0f / std::tan(fov_ * 0.5f);
    const float nf = 1.0f / (znear_ - zfar_);

    m4f m = m4f::Zero();

    // RH coordinate system, forward is -Z
    // NDC z ∈ [0, 1] (D3D/Vulkan style)
    m(0, 0) = f / aspect;
    m(1, 1) = f;

    m(2, 2) = zfar_ * nf;
    m(2, 3) = (zfar_ * znear_) * nf;
    m(3, 2) = -1.0f;

    return m;
}

m4f Camera_C::make_view(const transform& worldFromCamera) const
{
    return worldFromCamera.inverse().matrix();
}
}  // namespace batap
