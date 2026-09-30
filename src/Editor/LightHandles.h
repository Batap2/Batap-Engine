#pragma once

#include "Components/EntityHandle.h"
#include "EigenTypes.h"

namespace batap
{
struct Engine;
struct World;
struct Selection;

// Draggable points on the primary selected light's gizmo, Blender style: a
// point light's range, a spot's range and cone angles, a rect light's size,
// range and barn doors, a directional light's direction.
struct LightHandles
{
    // True while a handle has the mouse: the caller must skip entity picking.
    // Handles only draw, and never grab, when acceptInput is false.
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
    // What Escape puts back.
    float startValue_ = 0.f;
    float startSecond_ = 0.f;
    quatf startRot_ = quatf::Identity();
    v3f startAxis_ = v3f::UnitX();
};
}  // namespace batap
