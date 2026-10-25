#include "Camera_C.h"

#include "Systems/Camera_S.h"

#include <cmath>

namespace batap
{
void Camera_C::afterDeserialize(EntityHandle h, World&)
{
    if (h.get<Camera_C>().active_)
        Camera_S::activate(*h.reg_, h.entity_);
}

float exposureForEv100(float ev100)
{
    return 1.0f / (1.2f * std::exp2(ev100));
}

float ev100ForExposure(float exposure)
{
    return -std::log2(1.2f * exposure);
}

m4f Camera_C::make_proj(float aspect) const
{
    if (aspect <= 0.0f || znear_ <= 0.0f || fov_ <= 0.0f)
        return m4f::Identity();

    const float f = 1.0f / std::tan(fov_ * 0.5f);

    m4f m = m4f::Zero();

    // RH coordinate system, forward is -Z. Reversed Z with no far plane:
    // NDC z = znear_ / depth, 1 at znear_, 0 at infinity; the scene passes
    // clear the depth to 0 and test GREATER.
    m(0, 0) = f / aspect;
    m(1, 1) = f;

    m(2, 3) = znear_;
    m(3, 2) = -1.0f;

    return m;
}

m4f Camera_C::make_view(const transform& worldFromCamera) const
{
    return worldFromCamera.inverse().matrix();
}
}  // namespace batap
