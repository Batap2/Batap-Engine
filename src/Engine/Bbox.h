#pragma once

#include "EigenTypes.h"

#include <limits>

namespace batap
{

struct AABB
{
    v3f min_ = v3f::Constant(std::numeric_limits<float>::max());
    v3f max_ = v3f::Constant(std::numeric_limits<float>::lowest());

    bool valid() const { return (min_.array() <= max_.array()).all(); }

    void extend(const v3f& p)
    {
        min_ = min_.cwiseMin(p);
        max_ = max_.cwiseMax(p);
    }

    void extend(const AABB& other)
    {
        min_ = min_.cwiseMin(other.min_);
        max_ = max_.cwiseMax(other.max_);
    }

    bool intersects(const AABB& other) const
    {
        return (min_.array() <= other.max_.array()).all() &&
               (other.min_.array() <= max_.array()).all();
    }

    bool contains(const v3f& p) const
    {
        return (min_.array() <= p.array()).all() && (p.array() <= max_.array()).all();
    }

    float surfaceArea() const
    {
        if (!valid())
            return 0.f;
        const v3f d = size();
        return 2.f * (d.x() * d.y() + d.y() * d.z() + d.z() * d.x());
    }

    v3f center() const { return (min_ + max_) * 0.5f; }
    v3f size() const { return max_ - min_; }
    v3f halfSize() const { return size() * 0.5f; }

    AABB transformed(const transform& xform) const
    {
        const v3f c = xform * center();
        const v3f h = xform.linear().cwiseAbs() * halfSize();
        return {c - h, c + h};
    }
};

}  // namespace batap
