#pragma once

#include "Components/EntityHandle.h"
#include "Components/RigidBody_C.h"
#include "EigenTypes.h"

namespace batap
{
struct Engine;
struct World;
struct Selection;
struct Transform_C;

struct ColliderHandles
{
    bool draw(World& world, Engine& ctx, const Selection& selection, bool acceptInput);

    int selected(EntityHandle owner) const;
    void select(EntityHandle owner, int shape);
    void clear();

    enum struct Kind
    {
        Face,
        Radius,
        HalfHeight
    };

   private:
    void apply(Shape& s, const Transform_C& tc, float along, bool symmetric) const;

    EntityHandle owner_;
    int shape_ = -1;

    bool dragging_ = false;
    Kind dragKind_ = Kind::Face;
    int dragAxis_ = 0;
    Shape startShape_;
    v3f dragCentre_ = v3f::Zero();
    v3f dragDir_ = v3f::UnitX();
};
}  // namespace batap
