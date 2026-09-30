#include "LightHandles.h"

#include "Components/DirectionalLight_C.h"
#include "Components/PointLight_C.h"
#include "Components/RectLight_C.h"
#include "Components/SpotLight_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "InputManager.h"
#include "Instance/InstanceManager.h"
#include "LightGizmos.h"
#include "Selection.h"
#include "Spatial/SpatialIndex.h"
#include "UI/ScreenSpace.h"
#include "World.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>

namespace batap
{
namespace
{
using Kind = LightHandles::Kind;

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kHalfPi = kPi * 0.5f;
constexpr float kDegrees = 180.f / kPi;
constexpr float kMinLength = 0.01f;
constexpr float kMinAngle = 0.001f;
// Barn doors opened past this are taken as none: the far face of the volume
// runs off to infinity, and no handle could sit on it.
constexpr float kSpreadOpen = 80.f / kDegrees;

constexpr float kHandleRadius = 5.f;
constexpr float kGrabPixels = 9.f;
constexpr ImU32 kHandleColor = IM_COL32(255, 230, 51, 255);
constexpr ImU32 kLitColor = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kOutlineColor = IM_COL32(8, 12, 15, 200);
constexpr ImU32 kLabelBg = IM_COL32(10, 18, 23, 225);

struct LightFrame
{
    v3f pos_;
    v3f x_;
    v3f y_;
    v3f fwd_;
};

struct HandlePoint
{
    Kind kind_;
    v3f pos_;
};

// Distance along the line (origin, dir) of its closest point to the ray.
std::optional<float> alongLine(const Ray& ray, const v3f& origin, const v3f& dir)
{
    const v3f w = origin - ray.origin_;
    const float b = dir.dot(ray.dir_);
    const float denom = 1.f - b * b;
    if (denom < 1e-5f)
        return std::nullopt;
    return (b * ray.dir_.dot(w) - dir.dot(w)) / denom;
}

std::optional<v3f> onPlane(const Ray& ray, const v3f& point, const v3f& normal)
{
    const float denom = normal.dot(ray.dir_);
    if (std::abs(denom) < 1e-5f)
        return std::nullopt;
    const float t = normal.dot(point - ray.origin_) / denom;
    if (t <= 0.f)
        return std::nullopt;
    return ray.origin_ + ray.dir_ * t;
}

// Where the ray meets the sphere, front side first, else its point nearest to
// the centre: dragging off the sphere's rim keeps turning.
v3f onSphere(const Ray& ray, const v3f& center, float radius)
{
    const v3f oc = ray.origin_ - center;
    const float b = oc.dot(ray.dir_);
    const float c = oc.squaredNorm() - radius * radius;
    const float disc = b * b - c;
    if (disc >= 0.f)
    {
        const float t = -b - std::sqrt(disc);
        if (t > 0.f)
            return ray.origin_ + ray.dir_ * t;
    }
    return ray.origin_ + ray.dir_ * std::max(-b, 0.f);
}

LightFrame frameOf(const Transform_C& tc)
{
    const transform xf = tc.world();
    return LightFrame{xf.translation(), xf.linear().col(0).normalized(), xf.linear().col(1).normalized(),
                 -xf.linear().col(2).normalized()};
}

v3f conePoint(const LightFrame& f, const v3f& side, float range, float fullAngle)
{
    const float half = std::min(fullAngle * 0.5f, kHalfPi);
    return f.pos_ + (f.fwd_ * std::cos(half) + side * std::sin(half)) * range;
}

float rectSpreadShown(const RectLight_C& r)
{
    return std::min(r.spreadAngle_ * 0.5f, kSpreadOpen);
}

void collect(World& world, const EntityHandle& ent, const LightFrame& f, const v3f& camRight,
             std::vector<HandlePoint>& out)
{
    auto& reg = world.registry_;
    const entt::entity e = ent.entity_;
    if (const auto* point = reg.try_get<PointLight_C>(e))
        out.push_back({Kind::PointRange, f.pos_ + camRight * point->radius_});
    if (const auto* spot = reg.try_get<SpotLight_C>(e))
    {
        out.push_back({Kind::SpotRange, f.pos_ + f.fwd_ * spot->radius_});
        out.push_back({Kind::SpotOuter, conePoint(f, f.x_, spot->radius_, spot->outerAngle_)});
        out.push_back({Kind::SpotInner, conePoint(f, f.y_, spot->radius_, spot->innerAngle_)});
    }
    if (const auto* rect = reg.try_get<RectLight_C>(e))
    {
        const float hw = rect->width_ * 0.5f;
        const float hh = rect->height_ * 0.5f;
        out.push_back({Kind::RectWidth, f.pos_ + f.x_ * hw});
        out.push_back({Kind::RectWidth, f.pos_ - f.x_ * hw});
        out.push_back({Kind::RectHeight, f.pos_ + f.y_ * hh});
        out.push_back({Kind::RectHeight, f.pos_ - f.y_ * hh});
        out.push_back({Kind::RectRange, f.pos_ + f.fwd_ * rect->radius_});
        const float grow = rect->radius_ * std::tan(rectSpreadShown(*rect));
        out.push_back({Kind::RectSpread, f.pos_ + f.fwd_ * rect->radius_ + f.x_ * (hw + grow)});
    }
    if (reg.all_of<DirectionalLight_C>(e))
        out.push_back({Kind::SunDirection, f.pos_ + f.fwd_ * sunArrowLength(world, f.pos_)});
}
}  // namespace

bool LightHandles::draw(World& world, Engine& ctx, const Selection& selection, bool acceptInput)
{
    const EntityHandle ent = selection.primary();
    if (!ent.valid() || !ent.try_get<Transform_C>() || (dragging_ && !(ent == dragged_)))
    {
        dragging_ = false;
        return false;
    }

    auto proj = screenProjector(world, ctx);
    if (!proj)
        return false;
    proj->frameSize_ = toPoints(proj->frameSize_);

    auto& reg = world.registry_;
    const LightFrame f = frameOf(ent.get<Transform_C>());
    std::vector<HandlePoint> handles;
    collect(world, ent, f, proj->camRight_, handles);
    if (handles.empty())
        return false;

    InputManager& input = *ctx.inputManager_;
    const v2i mousePx = input.mousePos();
    const v2f mouse = toPoints(v2f(static_cast<float>(mousePx.x()), static_cast<float>(mousePx.y())));
    const ImVec2 vpPos = ImGui::GetMainViewport()->Pos;
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    int hovered = -1;
    std::vector<std::optional<v2f>> screen(handles.size());
    float best = kGrabPixels;
    for (size_t i = 0; i < handles.size(); ++i)
    {
        screen[i] = proj->project(handles[i].pos_);
        if (!dragging_ && acceptInput && !ImGui::GetIO().WantCaptureMouse && screen[i])
        {
            const float d = (*screen[i] - mouse).norm();
            if (d < best)
            {
                best = d;
                hovered = static_cast<int>(i);
            }
        }
    }

    const auto markDirty = [&]
    {
        if (reg.all_of<PointLight_C>(ent.entity_))
            world.instances().markDirty<PointLight_C>(ent);
        if (reg.all_of<SpotLight_C>(ent.entity_))
            world.instances().markDirty<SpotLight_C>(ent);
        if (reg.all_of<RectLight_C>(ent.entity_))
            world.instances().markDirty<RectLight_C>(ent);
    };

    if (hovered >= 0 && input.pressed(MouseButton::Left))
    {
        dragging_ = true;
        dragged_ = ent;
        dragKind_ = handles[static_cast<size_t>(hovered)].kind_;
        startAxis_ = proj->camRight_;
        startRot_ = quatf(ent.get<Transform_C>().world().rotation());
        if (auto* p = reg.try_get<PointLight_C>(ent.entity_); p && dragKind_ == Kind::PointRange)
            startValue_ = p->radius_;
        if (auto* s = reg.try_get<SpotLight_C>(ent.entity_))
        {
            startValue_ = dragKind_ == Kind::SpotRange ? s->radius_ : s->outerAngle_;
            startSecond_ = s->innerAngle_;
        }
        if (auto* r = reg.try_get<RectLight_C>(ent.entity_))
        {
            startValue_ = dragKind_ == Kind::RectWidth    ? r->width_
                          : dragKind_ == Kind::RectHeight ? r->height_
                          : dragKind_ == Kind::RectRange  ? r->radius_
                                                          : r->spreadAngle_;
        }
    }

    std::array<char, 64> label{};
    if (dragging_)
    {
        const bool cancel = input.pressed(Key::Escape);
        const Ray ray = rayFromScreen(world, ctx, toPixels(mouse));
        auto* point = reg.try_get<PointLight_C>(ent.entity_);
        auto* spot = reg.try_get<SpotLight_C>(ent.entity_);
        auto* rect = reg.try_get<RectLight_C>(ent.entity_);

        switch (dragKind_)
        {
            case Kind::PointRange:
                if (point)
                {
                    if (cancel)
                        point->radius_ = startValue_;
                    else if (const auto s = alongLine(ray, f.pos_, startAxis_))
                        point->radius_ = std::max(std::abs(*s), kMinLength);
                    std::snprintf(label.data(), label.size(), "radius %.2f m", static_cast<double>(point->radius_));
                }
                break;
            case Kind::SpotRange:
                if (spot)
                {
                    if (cancel)
                        spot->radius_ = startValue_;
                    else if (const auto s = alongLine(ray, f.pos_, f.fwd_))
                        spot->radius_ = std::max(*s, kMinLength);
                    std::snprintf(label.data(), label.size(), "radius %.2f m", static_cast<double>(spot->radius_));
                }
                break;
            case Kind::SpotOuter:
            case Kind::SpotInner:
                if (spot)
                {
                    const bool outer = dragKind_ == Kind::SpotOuter;
                    const v3f side = outer ? f.x_ : f.y_;
                    if (cancel)
                    {
                        spot->outerAngle_ = outer ? startValue_ : spot->outerAngle_;
                        spot->innerAngle_ = startSecond_;
                    }
                    else if (const auto q = onPlane(ray, f.pos_, f.fwd_.cross(side)))
                    {
                        const v3f d = *q - f.pos_;
                        const float half =
                            std::clamp(std::atan2(d.dot(side), d.dot(f.fwd_)), kMinAngle, kHalfPi);
                        if (outer)
                        {
                            spot->outerAngle_ = half * 2.f;
                            spot->innerAngle_ = std::min(spot->innerAngle_, spot->outerAngle_);
                        }
                        else
                        {
                            spot->innerAngle_ = std::min(half * 2.f, spot->outerAngle_);
                        }
                    }
                    std::snprintf(label.data(), label.size(), "%s %.1f deg", outer ? "outer" : "inner",
                                  static_cast<double>((outer ? spot->outerAngle_ : spot->innerAngle_) * kDegrees));
                }
                break;
            case Kind::RectWidth:
            case Kind::RectHeight:
                if (rect)
                {
                    const bool width = dragKind_ == Kind::RectWidth;
                    float& size = width ? rect->width_ : rect->height_;
                    if (cancel)
                        size = startValue_;
                    else if (const auto s = alongLine(ray, f.pos_, width ? f.x_ : f.y_))
                        size = std::max(std::abs(*s) * 2.f, kMinLength);
                    std::snprintf(label.data(), label.size(), "%s %.2f m", width ? "width" : "height",
                                  static_cast<double>(size));
                }
                break;
            case Kind::RectRange:
                if (rect)
                {
                    if (cancel)
                        rect->radius_ = startValue_;
                    else if (const auto s = alongLine(ray, f.pos_, f.fwd_))
                        rect->radius_ = std::max(*s, kMinLength);
                    std::snprintf(label.data(), label.size(), "radius %.2f m", static_cast<double>(rect->radius_));
                }
                break;
            case Kind::RectSpread:
                if (rect)
                {
                    if (cancel)
                        rect->spreadAngle_ = startValue_;
                    else if (const auto q = onPlane(ray, f.pos_, f.fwd_.cross(f.x_)))
                    {
                        // The angle of the door's edge, from the face's own edge.
                        const v3f d = *q - f.pos_;
                        const float half = std::clamp(
                            std::atan2(d.dot(f.x_) - rect->width_ * 0.5f, d.dot(f.fwd_)), 0.f,
                            kHalfPi);
                        rect->spreadAngle_ = half >= kSpreadOpen ? kPi : half * 2.f;
                    }
                    if (rect->spreadAngle_ >= kPi)
                        std::snprintf(label.data(), label.size(), "spread open");
                    else
                        std::snprintf(label.data(), label.size(), "spread %.1f deg",
                                      static_cast<double>(rect->spreadAngle_ * kDegrees));
                }
                break;
            case Kind::SunDirection:
            {
                EntityHandle h = ent;
                if (cancel)
                    h.setRotation(startRot_, Space::World);
                else
                {
                    const float length = sunArrowLength(world, f.pos_);
                    const v3f to = (onSphere(ray, f.pos_, length) - f.pos_).normalized();
                    const v3f from = startRot_ * -v3f::UnitZ();
                    if (to.allFinite())
                        h.setRotation((quatf::FromTwoVectors(from, to) * startRot_).normalized(),
                                      Space::World);
                }
                const v3f dir = quatf(h.get<Transform_C>().world().rotation()) * -v3f::UnitZ();
                const float elevation = std::asin(std::clamp(-dir.y(), -1.f, 1.f)) * kDegrees;
                std::snprintf(label.data(), label.size(), "elevation %.1f deg", static_cast<double>(elevation));
                break;
            }
        }
        markDirty();

        if (cancel || !input.down(MouseButton::Left))
            dragging_ = false;
    }

    for (size_t i = 0; i < handles.size(); ++i)
    {
        if (!screen[i])
            continue;
        const bool lit = static_cast<int>(i) == hovered || (dragging_ && handles[i].kind_ == dragKind_);
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

    return dragging_ || hovered >= 0;
}
}  // namespace batap
