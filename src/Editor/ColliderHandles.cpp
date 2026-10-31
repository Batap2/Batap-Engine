#include "ColliderHandles.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "ColliderShapes.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "HandleMath.h"
#include "InputManager.h"
#include "Renderer/DebugDraw.h"
#include "Selection.h"
#include "Spatial/SpatialIndex.h"
#include "Systems/Physics_S.h"
#include "Systems/Systems.h"
#include "UI/ScreenSpace.h"
#include "World.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <vector>

namespace batap
{
namespace
{
using Kind = ColliderHandles::Kind;
using handleMath::alongLine;
using handleMath::distanceToSegment;

constexpr float kHandleRadius = 5.f;
constexpr float kGrabPixels = 9.f;
constexpr float kOutlinePixels = 6.f;
constexpr float kMinSize = 0.001f;
constexpr size_t kRingSegments = 32;
constexpr ImU32 kHandleColor = IM_COL32(255, 230, 51, 255);
constexpr ImU32 kLitColor = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kOutlineColor = IM_COL32(8, 12, 15, 200);
constexpr ImU32 kLabelBg = IM_COL32(10, 18, 23, 225);

struct GrabPoint
{
    Kind kind_;
    int axis_;
    v3f pos_;
    v3f dir_;
};

struct Segment
{
    v3f a_;
    v3f b_;
};

v3f axisOf(const quatf& rot, Eigen::Index k)
{
    v3f axis = v3f::Zero();
    axis[k] = 1.f;
    return rot * axis;
}

float sphereRadius(const Shape& s, const v3f& scale)
{
    return s.radius_ * scale.sum() / 3.f;
}

float capsuleRadius(const Shape& s, const v3f& scale)
{
    return s.radius_ * (scale.x() + scale.z()) * 0.5f;
}

void collect(const Shape& s, const Transform_C& tc, const v3f& camRight,
             std::vector<GrabPoint>& out)
{
    const colliderShape::Pose pose = colliderShape::worldPose(tc, s);
    switch (s.kind_)
    {
        case Shape::Kind::Box:
            for (Eigen::Index k = 0; k < 3; ++k)
                for (const float sign : {-1.f, 1.f})
                {
                    const v3f dir = axisOf(pose.rot_, k) * sign;
                    out.push_back({Kind::Face, static_cast<int>(k),
                                   pose.pos_ + dir * (s.halfExtents_[k] * pose.scale_[k]), dir});
                }
            break;
        case Shape::Kind::Sphere:
            out.push_back(
                {Kind::Radius, 0, pose.pos_ + camRight * sphereRadius(s, pose.scale_), camRight});
            break;
        case Shape::Kind::Capsule: {
            const v3f x = axisOf(pose.rot_, 0);
            out.push_back({Kind::Radius, 0, pose.pos_ + x * capsuleRadius(s, pose.scale_), x});
            for (const float sign : {-1.f, 1.f})
            {
                const v3f dir = axisOf(pose.rot_, 1) * sign;
                out.push_back({Kind::HalfHeight, 1,
                               pose.pos_ + dir * (s.halfHeight_ * pose.scale_.y()), dir});
            }
            break;
        }
        case Shape::Kind::Mesh:
            break;
    }
}

void outline(const Shape& s, const colliderShape::Pose& pose, AssetManager& assets,
             std::vector<Segment>& out)
{
    const transform xf = TRS_Transform(pose.pos_, pose.rot_, v3f::Ones());
    const v3f& scale = pose.scale_;

    const auto boxEdges = [&](const v3f& centre, const v3f& half)
    {
        std::array<v3f, 8> corners{};
        for (size_t i = 0; i < corners.size(); ++i)
            corners[i] = xf * v3f(centre + v3f((i & 1u) ? half.x() : -half.x(),
                                               (i & 2u) ? half.y() : -half.y(),
                                               (i & 4u) ? half.z() : -half.z()));
        for (size_t i = 0; i < corners.size(); ++i)
            for (const size_t bit : {size_t{1}, size_t{2}, size_t{4}})
                if (!(i & bit))
                    out.push_back({corners[i], corners[i | bit]});
    };
    const auto ring = [&](const v3f& centre, const v3f& u, const v3f& v, float r)
    {
        constexpr float step = 2.f * std::numbers::pi_v<float> / static_cast<float>(kRingSegments);
        for (size_t i = 0; i < kRingSegments; ++i)
        {
            const float a0 = step * static_cast<float>(i);
            const float a1 = a0 + step;
            out.push_back({xf * v3f(centre + (u * std::cos(a0) + v * std::sin(a0)) * r),
                           xf * v3f(centre + (u * std::cos(a1) + v * std::sin(a1)) * r)});
        }
    };

    switch (s.kind_)
    {
        case Shape::Kind::Box:
            boxEdges(v3f::Zero(), s.halfExtents_.cwiseProduct(scale));
            break;
        case Shape::Kind::Sphere: {
            const float r = sphereRadius(s, scale);
            ring(v3f::Zero(), v3f::UnitX(), v3f::UnitY(), r);
            ring(v3f::Zero(), v3f::UnitY(), v3f::UnitZ(), r);
            ring(v3f::Zero(), v3f::UnitZ(), v3f::UnitX(), r);
            break;
        }
        case Shape::Kind::Capsule: {
            const float r = capsuleRadius(s, scale);
            const v3f up = v3f::UnitY() * (s.halfHeight_ * scale.y());
            ring(up, v3f::UnitX(), v3f::UnitZ(), r);
            ring(-up, v3f::UnitX(), v3f::UnitZ(), r);
            for (const v3f& side :
                 {v3f(v3f::UnitX()), v3f(-v3f::UnitX()), v3f(v3f::UnitZ()), v3f(-v3f::UnitZ())})
                out.push_back({xf * v3f(side * r + up), xf * v3f(side * r - up)});
            break;
        }
        case Shape::Kind::Mesh:
            if (const Mesh* mesh = assets.get(s.mesh_); mesh && mesh->localBounds_.valid())
                boxEdges(mesh->localBounds_.center().cwiseProduct(scale),
                         mesh->localBounds_.halfSize().cwiseProduct(scale));
            break;
    }
}

int pickOutline(const RigidBody_C& rb, const Transform_C& tc, AssetManager& assets,
                const ScreenProjector& proj, const v2f& mouse)
{
    int best = -1;
    float bestDistance = kOutlinePixels;
    std::vector<Segment> segments;
    for (size_t i = 0; i < rb.shapes_.size(); ++i)
    {
        segments.clear();
        outline(rb.shapes_[i], colliderShape::worldPose(tc, rb.shapes_[i]), assets, segments);
        for (const Segment& seg : segments)
        {
            const auto a = proj.project(seg.a_);
            const auto b = proj.project(seg.b_);
            if (!a || !b)
                continue;
            const float d = distanceToSegment(mouse, *a, *b);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

void describe(const Shape& s, Kind kind, int axis, std::array<char, 64>& label)
{
    constexpr std::array<char, 3> kAxisNames{'x', 'y', 'z'};
    switch (kind)
    {
        case Kind::Face:
            std::snprintf(label.data(), label.size(), "%c %.3f m",
                          kAxisNames[static_cast<size_t>(axis)],
                          static_cast<double>(s.halfExtents_[axis] * 2.f));
            break;
        case Kind::Radius:
            std::snprintf(label.data(), label.size(), "radius %.3f m",
                          static_cast<double>(s.radius_));
            break;
        case Kind::HalfHeight:
            std::snprintf(label.data(), label.size(), "height %.3f m",
                          static_cast<double>(s.halfHeight_ * 2.f));
            break;
    }
}
}  // namespace

int ColliderHandles::selected(EntityHandle owner) const
{
    return owner.valid() && owner == owner_ ? shape_ : -1;
}

void ColliderHandles::select(EntityHandle owner, int shape)
{
    owner_ = owner;
    shape_ = shape;
    dragging_ = false;
}

void ColliderHandles::clear()
{
    owner_ = {};
    shape_ = -1;
    dragging_ = false;
}

void ColliderHandles::apply(Shape& s, const Transform_C& tc, float along, bool symmetric) const
{
    const v3f scale = tc.scale().cwiseAbs().cwiseMax(kMinSize);
    if (dragKind_ == Kind::Radius)
    {
        const float factor = s.kind_ == Shape::Kind::Sphere ? scale.sum() / 3.f
                                                            : (scale.x() + scale.z()) * 0.5f;
        s.radius_ = std::max(std::abs(along), kMinSize) / factor;
        return;
    }

    const Eigen::Index k = dragKind_ == Kind::Face ? static_cast<Eigen::Index>(dragAxis_) : 1;
    float& half = dragKind_ == Kind::Face ? s.halfExtents_[k] : s.halfHeight_;
    const float startHalf =
        (dragKind_ == Kind::Face ? startShape_.halfExtents_[k] : startShape_.halfHeight_) *
        scale[k];
    const float minHalf = kMinSize * scale[k];

    if (symmetric)
    {
        half = std::max(std::abs(along), minHalf) / scale[k];
        colliderShape::setWorldPos(s, tc, dragCentre_);
        return;
    }

    // The opposite face stays where it was: the centre follows the grabbed one.
    const float worldHalf = std::max(startHalf + along, 2.f * minHalf) * 0.5f;
    half = worldHalf / scale[k];
    colliderShape::setWorldPos(s, tc, dragCentre_ + dragDir_ * (worldHalf - startHalf));
}

bool ColliderHandles::draw(World& world, Engine& ctx, const Selection& selection, bool acceptInput)
{
    EntityHandle ent = selection.primary();
    auto* rb = ent.try_get<RigidBody_C>();
    const auto* tc = ent.try_get<Transform_C>();
    if (!rb || !tc || rb->shapes_.empty())
    {
        clear();
        return false;
    }
    if (!(ent == owner_))
        select(ent, -1);
    if (shape_ >= static_cast<int>(rb->shapes_.size()))
        shape_ = -1;

    auto proj = screenProjector(world, ctx);
    if (!proj)
        return false;
    proj->origin_ = toPoints(proj->origin_);
    proj->frameSize_ = toPoints(proj->frameSize_);

    AssetManager& assets = world.assets();
    DebugDraw& dbg = world.debugOverlay();
    const transform base = TRS_Transform(tc->worldPos(), tc->worldRot(), v3f::Ones());
    const v3f scale = tc->scale().cwiseAbs();
    const bool shown = world.systems().physics_->showColliders_;
    for (size_t i = 0; i < rb->shapes_.size(); ++i)
    {
        const bool lit = static_cast<int>(i) == shape_;
        if (lit || !shown)
            Physics_S::drawShape(dbg, assets, rb->shapes_[i], base, scale,
                                 lit ? colors::yellow : colors::grey);
    }

    InputManager& input = *ctx.inputManager_;
    const ImGuiIO& io = ImGui::GetIO();
    const bool mouseFree = acceptInput && !io.WantCaptureMouse;
    const v2i mousePx = input.mousePos();
    const v2f mouse =
        toPoints(v2f(static_cast<float>(mousePx.x()), static_cast<float>(mousePx.y())));
    const ImVec2 vpPos = ImGui::GetMainViewport()->Pos;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    if (shape_ >= 0 && !dragging_ && acceptInput && !io.WantTextInput && input.pressed(Key::Escape))
        shape_ = -1;

    std::vector<GrabPoint> handles;
    if (shape_ >= 0)
        collect(rb->shapes_[static_cast<size_t>(shape_)], *tc, proj->camRight_, handles);

    int hovered = -1;
    std::vector<std::optional<v2f>> screen(handles.size());
    float best = kGrabPixels;
    for (size_t i = 0; i < handles.size(); ++i)
    {
        screen[i] = proj->project(handles[i].pos_);
        if (dragging_ || !mouseFree || !screen[i])
            continue;
        const float d = (*screen[i] - mouse).norm();
        if (d < best)
        {
            best = d;
            hovered = static_cast<int>(i);
        }
    }

    bool picked = false;
    if (mouseFree && !dragging_ && input.pressed(MouseButton::Left))
    {
        if (hovered >= 0)
        {
            const GrabPoint& h = handles[static_cast<size_t>(hovered)];
            dragging_ = true;
            dragKind_ = h.kind_;
            dragAxis_ = h.axis_;
            dragDir_ = h.dir_;
            startShape_ = rb->shapes_[static_cast<size_t>(shape_)];
            dragCentre_ = colliderShape::worldPose(*tc, startShape_).pos_;
        }
        else if (const int hit = pickOutline(*rb, *tc, assets, *proj, mouse); hit >= 0)
        {
            shape_ = hit;
            picked = true;
        }
    }

    std::array<char, 64> label{};
    if (dragging_)
    {
        Shape& s = rb->shapes_[static_cast<size_t>(shape_)];
        const bool cancel = input.pressed(Key::Escape);
        if (cancel)
            s = startShape_;
        else if (const auto along =
                     alongLine(rayFromScreen(world, ctx, toPixels(mouse)), dragCentre_, dragDir_))
            apply(s, *tc, *along, input.down(Key::LAlt));
        describe(s, dragKind_, dragAxis_, label);
        world.registry_.patch<RigidBody_C>(ent.entity_);

        if (cancel || !input.down(MouseButton::Left))
            dragging_ = false;
    }

    for (size_t i = 0; i < handles.size(); ++i)
    {
        if (!screen[i])
            continue;
        const bool lit =
            static_cast<int>(i) == hovered ||
            (dragging_ && handles[i].kind_ == dragKind_ && handles[i].dir_.dot(dragDir_) > 0.99f);
        const ImVec2 c{screen[i]->x() + vpPos.x, screen[i]->y() + vpPos.y};
        const float r = lit ? kHandleRadius + 1.5f : kHandleRadius;
        dl->AddCircleFilled(c, r + 1.5f, kOutlineColor);
        dl->AddCircleFilled(c, r, lit ? kLitColor : kHandleColor);
    }

    if (label.front() != '\0')
    {
        const ImVec2 at{mouse.x() + vpPos.x + 16.f, mouse.y() + vpPos.y + 12.f};
        const ImVec2 size = ImGui::CalcTextSize(label.data());
        dl->AddRectFilled({at.x - 5.f, at.y - 3.f}, {at.x + size.x + 5.f, at.y + size.y + 3.f},
                          kLabelBg, 3.f);
        dl->AddText(at, kLitColor, label.data());
    }

    return dragging_ || hovered >= 0 || picked;
}
}  // namespace batap
