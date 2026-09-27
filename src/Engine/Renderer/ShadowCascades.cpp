#include "Renderer/ShadowCascades.h"

#include <algorithm>
#include <cmath>

namespace batap
{
namespace
{
constexpr float kSplitNear = 1.6f;
}  // namespace

ShadowFit fitShadowCascades(const ShadowFitInput& in)
{
    ShadowFit fit{};
    if (in.range_ <= kSplitNear || in.fovY_ <= 0.f || in.aspect_ <= 0.f || in.count_ == 0)
        return fit;

    const uint32_t count = std::min(in.count_, ShadowCascadeCount);
    const v3f fwd = in.camFwd_.normalized();

    // Squared distance from the axis to a frustum corner, per unit of depth.
    const float a = std::tan(in.fovY_ * 0.5f);
    const float b = a * in.aspect_;
    const float k2 = a * a + b * b;

    float near = kSplitNear;
    for (uint32_t i = 0; i < count; ++i)
    {
        const float far =
            i + 1 == count
                ? in.range_
                : kSplitNear * std::pow(in.range_ / kSplitNear,
                                        static_cast<float>(i + 1) / static_cast<float>(count));

        const float c = std::min((near + far) * 0.5f * (1.f + k2), far);
        const float r = std::sqrt(std::max((near - c) * (near - c) + near * near * k2,
                                           (far - c) * (far - c) + far * far * k2));

        CascadeSphere& s = fit.cascades_[i];
        s.center_ = in.camPos_ + fwd * c;
        s.radius_ = r;
        s.near_ = near;
        s.far_ = far;
        s.texelWorld_ = 2.f * r / static_cast<float>(CascadeTileSize);

        near = far;
    }
    fit.count_ = count;
    return fit;
}
}  // namespace batap
