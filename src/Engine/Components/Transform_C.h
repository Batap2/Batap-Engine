#pragma once
#include <vector>
#include "Components/EntityHandle.h"
#include "EigenTypes.h"

namespace batap
{

struct Transform_S;
struct World;

struct Transform_C
{
    // The fields below are private, so the reflection registry cannot
    // discover them: registration is spelled out by hand from inside the
    // class, where the member pointers are accessible. Defined in the .cpp.
    static void registerReflection();

    // Deserialization writes the local pos/rot/scale straight through the
    // field offsets, bypassing Transform_S — this puts the entity back in the
    // dirty list so the local and world matrices get rebuilt.
    static void afterDeserialize(EntityHandle h, World& world);

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
