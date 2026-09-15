#pragma once

#include "Components/EntityHandle.h"
#include "EigenTypes.h"
#include "Selection.h"

#include <array>
#include <vector>

namespace batap
{
struct Engine;
struct World;

enum struct GizmoMode
{
    Translate,
    Rotate,
    Scale
};

enum struct GizmoPivot
{
    Origin,
    Center
};

struct Gizmo
{
    // True while the gizmo has the mouse: the caller must skip entity picking.
    bool draw(World& world, Engine& ctx, const Selection& selection);

    GizmoMode mode_ = GizmoMode::Translate;
    GizmoPivot pivot_ = GizmoPivot::Origin;
    bool local_ = false;

    float snapUnits_ = 1.f;
    float snapDegrees_ = 15.f;
    float snapScale_ = 0.25f;

   private:
    struct Target
    {
        EntityHandle handle_;
        v3f pos_ = v3f::Zero();
        v3f scale_ = v3f::Ones();
        quatf rot_ = quatf::Identity();
    };

    std::vector<Target> targets_;
    std::array<float, 8> glow_{};
    std::array<char, 16> typed_{};

    v3f dragStartPivot_ = v3f::Zero();
    v3f dragPrevRaw_ = v3f::Zero();
    v3f dragAccum_ = v3f::Zero();
    v3f dragAxis_ = v3f::UnitX();
    v3f dragDelta_ = v3f::Zero();
    quatf dragAccumRot_ = quatf::Identity();

    v2f virtualMouse_ = v2f::Zero();

    float dragStartAngle_ = 0.f;
    float dimFade_ = 0.f;
    int typedLen_ = 0;
    int hovered_ = -1;
    int dragId_ = -1;
};
}  // namespace batap
