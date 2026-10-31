#pragma once

#include "EigenTypes.h"
#include "Spatial/Ray.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace batap::handleMath
{
inline std::optional<float> alongLine(const Ray& ray, const v3f& origin, const v3f& dir)
{
    const v3f w = origin - ray.origin_;
    const float b = dir.dot(ray.dir_);
    const float denom = 1.f - b * b;
    if (denom < 1e-5f)
        return std::nullopt;
    return (b * ray.dir_.dot(w) - dir.dot(w)) / denom;
}

inline std::optional<v3f> onPlane(const Ray& ray, const v3f& point, const v3f& normal)
{
    const float denom = normal.dot(ray.dir_);
    if (std::abs(denom) < 1e-5f)
        return std::nullopt;
    const float t = normal.dot(point - ray.origin_) / denom;
    if (t <= 0.f)
        return std::nullopt;
    return ray.origin_ + ray.dir_ * t;
}

inline float distanceToSegment(const v2f& p, const v2f& a, const v2f& b)
{
    const v2f ab = b - a;
    const float len2 = ab.squaredNorm();
    if (len2 < 1e-6f)
        return (p - a).norm();

    const float t = std::clamp((p - a).dot(ab) / len2, 0.f, 1.f);
    return (p - (a + ab * t)).norm();
}
}  // namespace batap::handleMath
