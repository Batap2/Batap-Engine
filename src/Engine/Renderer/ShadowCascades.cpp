#include "Renderer/ShadowCascades.h"

#include <algorithm>
#include <cmath>

namespace batap
{
namespace
{
constexpr float kSplitNear = 1.6f;

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
    if (in.fovY_ <= 0.f || in.aspect_ <= 0.f || in.count_ == 0)
        return fit;

    const uint32_t count = std::min(in.count_, ShadowCascadeCount);
    const auto farOf = [&](uint32_t i)
    {
        const float t = static_cast<float>(i + 1) / static_cast<float>(count);
        return in.range_ * t * t;
    };
    if (farOf(0) <= kSplitNear)
        return fit;
    const v3f fwd = in.camFwd_.normalized();

    // Squared distance from the axis to a frustum corner, per unit of depth.
    const float a = std::tan(in.fovY_ * 0.5f);
    const float b = a * in.aspect_;
    const float k2 = a * a + b * b;

    float near = kSplitNear;
    for (uint32_t i = 0; i < count; ++i)
    {
        const float far = farOf(i);

        const float c = std::min((near + far) * 0.5f * (1.f + k2), far);
        const float r = std::sqrt(std::max((near - c) * (near - c) + near * near * k2,
                                           (far - c) * (far - c) + far * far * k2));

        const v3f center = in.camPos_ + fwd * c;
        CascadeSphere& s = fit.cascades_[i];
        s.center_ = center;
        s.near_ = near;
        s.far_ = far;
        near = far;

        // Inside the sphere the light sits between casters and receivers: no
        // projection is right.
        s.lightDistance_ = (in.lightPos_ - center).norm() / r;
        if (s.lightDistance_ <= 1.f)
            continue;

        // From the centre rounded to a world grid of one radius: the snapping
        // grid below is anchored at the world origin, so a frame turning at
        // every step would swing it under the camera by distance-to-origin *
        // angle. It only turns when the sphere changes cell.
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

m4f cascadeViewProj(const CascadeSphere& s)
{
    const float r = s.radius_;
    const v3f eye = s.center_ + s.lightFrame_.row(2).transpose() * r;

    m4f view = m4f::Identity();
    view.block<3, 3>(0, 0) = s.lightFrame_;
    view.block<3, 1>(0, 3) = -(s.lightFrame_ * eye);

    // Near at the sphere's edge: casters between it and the light land on depth
    // 0 through depth clamp, they are not clipped.
    m4f proj = m4f::Zero();
    proj(0, 0) = 1.f / r;
    proj(1, 1) = 1.f / r;
    proj(2, 2) = -1.f / (2.f * r);
    proj(3, 3) = 1.f;
    return m4f{proj * view};
}
}  // namespace batap
