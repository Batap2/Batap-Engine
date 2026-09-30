#include "Renderer/ShadowCascades.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace batap
{
namespace
{
constexpr float kSplitNear = 1.6f;

// The lattice axis is the direction from the light to the sphere's centre,
// rounded on a grid of this step on the face of the cube it points through:
// cells 5 degrees wide at a face's centre, narrower toward its edges, and no
// pole where a polar grid would turn at every step under a light overhead.
// Coarse, so it turns rarely: every 370 m of sideways travel under a light
// 4.2 km away, never under a sun. Every turn re-lays the texels, and every
// edge may move by half a texel at once; the shadow itself does not move.
constexpr double kAxisStep = 0.087488663525924;  // tan(5 deg)
// The lattice step follows the distance to the light on a geometric grid of
// this many steps per octave, 4.4 % apart: one change per 190 m of approach
// at 4.2 km, with the same re-laying.
constexpr double kTexelStepsPerOctave = 16.0;
// Depth range about the sphere, in radii.
constexpr double kDepthMargin = 1.05;
// The gnomonic plane cannot hold a cone that reaches its own horizon: capped
// there, a light almost inside the sphere gets a huge texel, not a NaN. R7
// already reports it.
constexpr double kMaxHalfAngle = 89.0 * 3.14159265358979323846 / 180.0;

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

v3d quantizedAxis(const v3d& a)
{
    int major = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs(a[i]) > std::abs(a[major]))
            major = i;
    v3d q = a / std::abs(a[major]);
    for (int i = 0; i < 3; ++i)
        if (i != major)
            q[i] = std::round(q[i] / kAxisStep) * kAxisStep;
    return q.normalized();
}

// One direction for every cascade and every frame: the frame never turns, and
// the window, rounded to the texel on its three axes, only ever moves the grid
// by whole texels. The window holds the sphere plus the half texel its centre
// is rounded by.
void fitDirectional(CascadeSphere& s, const m3f& frame, double radius)
{
    const double halfTile = static_cast<double>(CascadeTileSize) * 0.5;
    const float texel = static_cast<float>(radius / (halfTile - 0.5));
    const double texelD = static_cast<double>(texel);
    const v3d p = frame.cast<double>() * s.center_.cast<double>();
    const v3d snapped = (p / texelD).array().round().matrix() * texelD;

    s.radius_ = static_cast<float>(radius);
    s.texelWorld_ = texel;
    s.lightDistance_ = std::numeric_limits<float>::infinity();
    s.directional_ = true;
    s.lightFrame_ = frame;
    s.window_ = snapped.head<2>();
    s.depthNear_ = snapped.z() + radius * kDepthMargin;
    s.depthFar_ = snapped.z() - radius * kDepthMargin;
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

    // The sphere's true direction sits at most half a cell's diagonal off the
    // lattice axis.
    const double betaMax = std::atan(kAxisStep / std::sqrt(2.0));
    const double halfTile = static_cast<double>(CascadeTileSize) * 0.5;

    const bool directional = in.lightDir_ != v3f::Zero();
    const m3f sunFrame = directional ? lightFrame(v3f{-in.lightDir_.normalized()})
                                     : m3f{m3f::Identity()};

    float near = kSplitNear;
    for (uint32_t i = 0; i < count; ++i)
    {
        const float far = farOf(i);

        // Distance along the view axis to the smallest sphere holding the
        // slice, and its radius.
        const float c = std::min((near + far) * 0.5f * (1.f + k2), far);
        const float r = std::sqrt(std::max((near - c) * (near - c) + near * near * k2,
                                           (far - c) * (far - c) + far * far * k2));

        const v3f center = in.camPos_ + fwd * c;
        const double rd = static_cast<double>(r);
        CascadeSphere& s = fit.cascades_[i];
        s.center_ = center;
        s.near_ = near;
        s.far_ = far;
        near = far;

        if (directional)
        {
            fitDirectional(s, sunFrame, static_cast<double>(r));
            continue;
        }

        // Everything measured from the light is in double: at a star's
        // distance, a float step is longer than the sphere.
        const v3d toCenter = center.cast<double>() - in.lightPos_.cast<double>();
        const double dist = toCenter.norm();
        s.lightDistance_ = static_cast<float>(dist / rd);
        // Inside the sphere the light sits between casters and receivers: no
        // projection is right.
        if (s.lightDistance_ <= 1.f)
            continue;

        const v3d axis = quantizedAxis(toCenter / dist);
        const m3f frame = lightFrame(v3f{(-axis).cast<float>()});
        const v3d p = frame.cast<double>() * toCenter;
        const double depth = -p.z();

        // The sphere is a cone of half-angle alpha from the light. On the
        // gnomonic plane, with its axis up to betaMax off the lattice axis, it
        // reaches this far from its centre's own image; the window must hold
        // that plus the half step the centre is rounded by.
        const double alpha = std::asin(std::min(rd / dist, 1.0));
        const double edge = std::min(betaMax + alpha, kMaxHalfAngle);
        const double halfNeeded = std::tan(edge) - std::tan(betaMax);
        const double texelNeeded = halfNeeded / (halfTile - 0.5);
        const float texel = static_cast<float>(std::exp2(
            std::ceil(std::log2(texelNeeded) * kTexelStepsPerOctave) / kTexelStepsPerOctave));
        // The step the GPU sees: the window is a whole number of these.
        const double texelD = static_cast<double>(texel);

        s.radius_ = r;
        s.texelWorld_ = texel;
        s.lightFrame_ = frame;
        s.lightPos_ = in.lightPos_;
        s.window_ = (p.head<2>() / (depth * texelD)).array().round().matrix() * texelD;
        s.depthNear_ = std::max(depth - rd * kDepthMargin, 1e-3 * rd);
        s.depthFar_ = depth + rd * kDepthMargin;
    }
    fit.count_ = count;
    return fit;
}

m4d cascadeViewProjExact(const CascadeSphere& s)
{
    const m3d frame = s.lightFrame_.cast<double>();
    const double halfWidth = static_cast<double>(s.texelWorld_) * CascadeTileSize * 0.5;

    if (s.directional_)
    {
        // Orthographic: x = (p - window) / halfWidth in the light's frame, the
        // sphere's side facing the light at depth 0, the far side at 1.
        const double depthSpan = s.depthNear_ - s.depthFar_;
        m4d vp = m4d::Zero();
        vp.block<1, 3>(0, 0) = frame.row(0) / halfWidth;
        vp(0, 3) = -s.window_.x() / halfWidth;
        vp.block<1, 3>(1, 0) = frame.row(1) / halfWidth;
        vp(1, 3) = -s.window_.y() / halfWidth;
        vp.block<1, 3>(2, 0) = -frame.row(2) / depthSpan;
        vp(2, 3) = s.depthNear_ / depthSpan;
        vp(3, 3) = 1.0;
        return vp;
    }

    m4d view = m4d::Identity();
    view.block<3, 3>(0, 0) = frame;
    view.block<3, 1>(0, 3) = -(frame * s.lightPos_.cast<double>());

    // Off-centre perspective: x = (u - window) / halfWidth with u = x / depth,
    // the sphere's side facing the light at depth 0, the far side at 1.
    const double nf = 1.0 / (s.depthNear_ - s.depthFar_);
    m4d proj = m4d::Zero();
    proj(0, 0) = 1.0 / halfWidth;
    proj(0, 2) = s.window_.x() / halfWidth;
    proj(1, 1) = 1.0 / halfWidth;
    proj(1, 2) = s.window_.y() / halfWidth;
    proj(2, 2) = s.depthFar_ * nf;
    proj(2, 3) = s.depthFar_ * s.depthNear_ * nf;
    proj(3, 2) = -1.0;
    return m4d{proj * view};
}

m4f cascadeViewProj(const CascadeSphere& s)
{
    // Rounded once at the end: the entries then hold to a hundredth of a texel
    // on the GPU under a light 4 km away. Ten times farther, ten times worse;
    // a sun is a DirectionalLight_C.
    return m4f{cascadeViewProjExact(s).cast<float>()};
}
}  // namespace batap
