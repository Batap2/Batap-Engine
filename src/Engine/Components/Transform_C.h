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

    const transform& local() const { return local_; }
    const transform& world() const { return world_; }
    m4f worldMatrix() const { return world_.matrix(); }
    m4f localMatrix() const { return local_.matrix(); }

    const v3f&   pos()   const { return localPosition_; }
    const quatf& rot()   const { return localRotation_; }
    const v3f&   scale() const { return localScale_; }

    // No constructor for entt
    static Transform_C fromPosRotScale(v3f pos, quatf rot, v3f scale)
    {
        Transform_C t;
        t.localPosition_ = pos;
        t.localRotation_ = rot;
        t.localScale_    = scale;
        return t;
    }

   private:
    friend struct Transform_S;

    v3f localPosition_{0.f, 0.f, 0.f};
    quatf localRotation_ = quatf::Identity();
    v3f localScale_{1.f, 1.f, 1.f};

    uint32_t dirtyStamp_ = 0;
    bool localDirty_ = true;
    transform local_ = transform::Identity();
    transform world_ = transform::Identity();

    void setLocalFromTransform(const transform& t);
    static quatf extractWorldRotation(const transform& t);
};
}  // namespace batap
