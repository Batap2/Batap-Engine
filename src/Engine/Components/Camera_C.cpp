#include "Camera_C.h"

#include "Systems/Camera_S.h"

namespace batap
{
void Camera_C::afterDeserialize(EntityHandle h, World&)
{
    if (h.get<Camera_C>().active_)
        Camera_S::activate(*h.reg_, h.entity_);
}

m4f Camera_C::make_proj(float aspect) const
{
    if (aspect <= 0.0f || znear_ <= 0.0f || zfar_ <= znear_ || fov_ <= 0.0f)
        return m4f::Identity();

    const float f = 1.0f / std::tan(fov_ * 0.5f);
    const float nf = 1.0f / (zfar_ - znear_);

    m4f m = m4f::Zero();

    // RH coordinate system, forward is -Z. Reversed Z: NDC z = 1 at znear_,
    // 0 at zfar_; the scene passes clear the depth to 0 and test GREATER.
    m(0, 0) = f / aspect;
    m(1, 1) = f;

    m(2, 2) = znear_ * nf;
    m(2, 3) = (zfar_ * znear_) * nf;
    m(3, 2) = -1.0f;

    return m;
}

m4f Camera_C::make_view(const transform& worldFromCamera) const
{
    return worldFromCamera.inverse().matrix();
}
}  // namespace batap
