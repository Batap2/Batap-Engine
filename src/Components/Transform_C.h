#pragma once
#include <vector>
#include "Components/EntityHandle.h"
#include "EigenTypes.h"

namespace rayvox
{

struct TransformSystem;

struct Transform_C
{
    EntityHandle _parent{};
    std::vector<EntityHandle> children;

    EntityHandle parent() const { return _parent; }
    const transform& local() const { return _local; }
    const transform& world() const { return _world; }
    m4f worldMatrix() const { return _world.matrix(); }
    m4f localMatrix() const { return _local.matrix(); }

   private:
    friend struct TransformSystem;

    v3f _localPosition{0.f, 0.f, 0.f};
    quatf _localRotation = quatf::Identity();
    v3f _localScale{1.f, 1.f, 1.f};

    uint32_t _dirtyStamp = 0;
    bool _localDirty = true;
    transform _local = transform::Identity();
    transform _world = transform::Identity();

    void setLocalFromTransform(const transform& t)
    {
        _localPosition = t.translation();

        const m3f A = t.linear();
        float sx = A.col(0).norm();
        if (sx == 0.f)
            sx = 1.f;
        float sy = A.col(1).norm();
        if (sy == 0.f)
            sy = 1.f;
        float sz = A.col(2).norm();
        if (sz == 0.f)
            sz = 1.f;

        _localScale = {sx, sy, sz};

        m3f R;
        R.col(0) = A.col(0) / sx;
        R.col(1) = A.col(1) / sy;
        R.col(2) = A.col(2) / sz;

        _localRotation = quatf(R).normalized();
    }

    static quatf extractWorldRotation(const transform& t)
    {
        m3f R = t.linear();
        float sx = R.col(0).norm();
        if (sx == 0.f)
            sx = 1.f;
        float sy = R.col(1).norm();
        if (sy == 0.f)
            sy = 1.f;
        float sz = R.col(2).norm();
        if (sz == 0.f)
            sz = 1.f;
        R.col(0) /= sx;
        R.col(1) /= sy;
        R.col(2) /= sz;
        return quatf(R).normalized();
    }
};
}  // namespace rayvox
