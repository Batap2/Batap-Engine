#pragma once

#include <vector>

#include "EigenTypes.h"
#include "Handles.h"

namespace rayvox
{

enum class Space
{
    Local,
    Parent,
    World
};

struct Transform_C
{
    Transform_C* _parent = nullptr;
    std::vector<Transform_C*> children;

    void setParent(Transform_C* newParent, bool keepWorld = true);

    Transform_C* parent() const { return _parent; }

    const transform& local() const { return _local; }
    const transform& world() const { return _world; }
    m4f worldMatrix() const { return _world.matrix(); }
    m4f localMatrix() const { return _local.matrix(); }

    // mark dirty
    void setLocalPosition(const v3f& p);
    void setLocalRotation(const quatf& q);
    void setLocalScale(const v3f& s);

    void translate(const v3f& vec, Space space = Space::Local);
    void rotate(const quatf& delta, Space space = Space::Local);
    void rotate(const v3f& axis, float radians, Space space = Space::Local);
    void scale(const v3f& vec);

    void updateIfDirty();
    void markDirty();

   private:
    v3f _localPosition{0.f, 0.f, 0.f};
    quatf _localRotation = quatf::Identity();
    v3f _localScale{1.f, 1.f, 1.f};

    bool _dirty = true;
    transform _local = transform::Identity();
    transform _world = transform::Identity();

    void updateWorldMatrix_NoRecurse();
    void setLocalFromTransform(const transform& t);
    static quatf extractWorldRotation(const transform& t);
};

}  // namespace rayvox
