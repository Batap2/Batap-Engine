#pragma once

#include "Bbox.h"
#include "EigenTypes.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <limits>

namespace batap
{

struct Ray
{
    v3f origin_ = v3f::Zero();
    v3f dir_ = -v3f::UnitZ();
    float tMin_ = 0.f;
    float tMax_ = std::numeric_limits<float>::max();
};

struct RayHit
{
    entt::entity entity_ = entt::null;
    float t_ = 0.f;
    v3f point_ = v3f::Zero();
    v3f normal_ = v3f::Zero();

    bool hit() const { return entity_ != entt::null; }
};

struct AABBHit
{
    float tNear_ = 0.f;
    float tFar_ = 0.f;
    int entryAxis_ = -1;
    int exitAxis_ = -1;
    bool hit_ = false;
};

inline AABBHit rayAABB(const v3f& origin, const v3f& invDir, float tMin, float tMax,
                       const AABB& box)
{
    AABBHit out;
    int entryAxis = -1;
    int exitAxis = -1;

    for (int i = 0; i < 3; ++i)
    {
        float t0 = (box.min_[i] - origin[i]) * invDir[i];
        float t1 = (box.max_[i] - origin[i]) * invDir[i];
        if (t0 > t1)
            std::swap(t0, t1);
        if (t0 > tMin)
        {
            tMin = t0;
            entryAxis = i;
        }
        if (t1 < tMax)
        {
            tMax = t1;
            exitAxis = i;
        }
        if (tMin > tMax)
            return out;
    }

    out.hit_ = true;
    out.tNear_ = tMin;
    out.tFar_ = tMax;
    out.entryAxis_ = entryAxis;
    out.exitAxis_ = exitAxis;
    return out;
}

inline v3f aabbFaceNormal(int axis, const v3f& dir, bool entering)
{
    if (axis < 0)
        return -dir.normalized();
    v3f n = v3f::Zero();
    const float sign = dir[axis] > 0.f ? -1.f : 1.f;
    n[axis] = entering ? sign : -sign;
    return n;
}

}  // namespace batap
