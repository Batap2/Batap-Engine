#pragma once
#include <vector>
#include "Components/EntityHandle.h"
#include "EigenTypes.h"

namespace batap
{

struct Transform_S;

struct Transform_C
{
    const transform& local() const { return _local; }
    const transform& world() const { return _world; }
    m4f worldMatrix() const { return _world.matrix(); }
    m4f localMatrix() const { return _local.matrix(); }

    const v3f&   pos()   const { return _localPosition; }
    const quatf& rot()   const { return _localRotation; }
    const v3f&   scale() const { return _localScale; }

    // No constructor for entt
    static Transform_C fromPosRotScale(v3f pos, quatf rot, v3f scale)
    {
        Transform_C t;
        t._localPosition = pos;
        t._localRotation = rot;
        t._localScale    = scale;
        return t;
    }

   private:
    friend struct Transform_S;

    v3f _localPosition{0.f, 0.f, 0.f};
    quatf _localRotation = quatf::Identity();
    v3f _localScale{1.f, 1.f, 1.f};

    uint32_t _dirtyStamp = 0;
    bool _localDirty = true;
    transform _local = transform::Identity();
    transform _world = transform::Identity();

    void setLocalFromTransform(const transform& t);
    static quatf extractWorldRotation(const transform& t);
};
}  // namespace batap
