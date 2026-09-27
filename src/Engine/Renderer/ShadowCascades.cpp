#include "Renderer/ShadowCascades.h"

#include <algorithm>
#include <cmath>

namespace batap
{
namespace
{
constexpr float kSplitNear = 1.6f;
// Closer than this, the light has no single direction to light the sphere
// from: the cascade switches off instead of producing a wrong matrix.
constexpr float kMinLightDistanceInRadii = 4.f;

m3f lightFrame(const v3f& dir)
{
    const v3f upRef = std::abs(dir.z()) < 0.99f ? v3f::UnitZ() : v3f::UnitX();
    const v3f x = upRef.cross(dir).normalized();
    m3f frame;
    frame.row(0) = x.transpose();
    frame.row(1) = dir.cross(x).transpose();
    frame.row(2) = dir.transpose();
    return frame;
}
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

        const v3f center = in.camPos_ + fwd * c;
        CascadeSphere& s = fit.cascades_[i];
        s.center_ = center;
        s.near_ = near;
        s.far_ = far;
        near = far;

        if ((in.lightPos_ - center).norm() < r * kMinLightDistanceInRadii)
            continue;

        // Per cascade, from the sphere to the light: what lets a positional
        // light drive cascades. A directional light is the constant case.
        // Taken from the centre rounded to a world grid of one radius, so the
        // frame only turns when the sphere changes cell: the snapping grid
        // below is anchored at the world origin, and a frame turning at every
        // step would swing it under the camera by distance-to-origin * angle.
        const v3f zone = (center / r).array().round().matrix() * r;
        const m3f frame = lightFrame((in.lightPos_ - zone).normalized());
        const float texel = 2.f * r / static_cast<float>(CascadeTileSize);

        // Snapping pins the texel grid to the world, or every camera step
        // slides it and the shadow edges swim. On all three axes: the third
        // fixes the depth every caster is stored at.
        const v3f snapped = ((frame * center) / texel).array().round().matrix() * texel;
        s.center_ = frame.transpose() * snapped;
        s.radius_ = r;
        s.texelWorld_ = texel;
        s.lightFrame_ = frame;
    }
    fit.count_ = count;
    return fit;
}
}  // namespace batap
