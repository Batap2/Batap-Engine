#pragma once

#include "Components/EntityHandle.h"
#include "EigenTypes.h"

namespace batap
{
struct Engine;
struct World;
struct Selection;

struct LightHandles
{
    bool draw(World& world, Engine& ctx, const Selection& selection, bool acceptInput);

    enum struct Kind
    {
        PointRange,
        SpotRange,
        SpotOuter,
        SpotInner,
        RectWidth,
        RectHeight,
        RectRange,
        RectSpread,
        SunDirection,
    };

   private:
    EntityHandle dragged_;
    Kind dragKind_ = Kind::PointRange;
    bool dragging_ = false;
    float startValue_ = 0.f;
    float startSecond_ = 0.f;
    quatf startRot_ = quatf::Identity();
    v3f startAxis_ = v3f::UnitX();
};
}  // namespace batap
