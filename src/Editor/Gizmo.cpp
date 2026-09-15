#include "Gizmo.h"

#include "Components/Transform_C.h"
#include "Engine.h"
#include "InputManager.h"
#include "Platform/PlatformWindow.h"
#include "Spatial/SpatialIndex.h"
#include "Systems/Hierarchy_S.h"
#include "World.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace batap
{
namespace
{
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;
constexpr float kDegrees = 180.f / kPi;

constexpr float kAxisPixels = 88.f;
constexpr float kHeadPixels = 17.f;
constexpr float kHeadHalfWidth = 6.5f;
constexpr float kBoxPixels = 5.5f;
constexpr float kGrabPixels = 9.f;
constexpr float kShaftThickness = 2.5f;
constexpr float kShaftLit = 4.f;
constexpr float kOutlineGrow = 1.6f;
constexpr float kCenterRadius = 3.f;
constexpr float kScreenRadius = 16.f;
constexpr float kDimAlpha = 0.28f;
constexpr float kBackRingAlpha = 0.16f;
constexpr float kPlaneFillAlpha = 0.25f;
constexpr float kPlaneInner = 0.3f;
constexpr float kPlaneOuter = 0.62f;
constexpr float kRingOuterScale = 1.22f;
constexpr float kTrackballRadius = kAxisPixels * kRingOuterScale - 9.f;
// A quarter turn from the centre of the disc to its rim.
constexpr float kTrackballGain = 0.5f / kTrackballRadius;
constexpr float kEdgeOnDot = 0.15f;

constexpr float kMinAxisPixels = 26.f;

constexpr float kGlowTau = 0.045f;
constexpr float kDashLen = 6.f;
constexpr float kDashGap = 5.f;
constexpr float kTickHalf = 5.f;
constexpr float kTickMinSpacing = 7.f;
constexpr int kTickSpan = 12;
constexpr float kMarkerArm = 5.f;
constexpr float kWrapMargin = 12.f;

constexpr float kPrecision = 0.01f;
constexpr float kMinScale = 0.001f;

constexpr float kMinAxisDenom = 0.02f;
constexpr float kMinPlaneDenom = 0.05f;
constexpr float kMaxStepAxes = 6.f;
constexpr float kMaxStepAngle = 1.f;

constexpr size_t kRingSegments = 72;
using RingPoints = std::array<v2f, kRingSegments + 1>;
using RingFront = std::array<bool, kRingSegments + 1>;

constexpr int kNone = -1;
constexpr int kAxisCount = 3;
constexpr int kPlaneFirst = 3;
constexpr int kScreenId = 6;
constexpr int kTrackballId = 7;
constexpr size_t kHandleCount = 8;

bool isAxis(int id)
{
    return id >= 0 && id < kAxisCount;
}
bool isPlane(int id)
{
    return id >= kPlaneFirst && id < kScreenId;
}
size_t idAxis(int id)
{
    return static_cast<size_t>(isPlane(id) ? id - kPlaneFirst : id);
}
Eigen::Index toIndex(size_t i)
{
    return static_cast<Eigen::Index>(i);
}

constexpr std::array<ImVec4, 3> kAxisColors{ImVec4{0.91f, 0.29f, 0.33f, 1.f},
                                            ImVec4{0.55f, 0.79f, 0.24f, 1.f},
                                            ImVec4{0.25f, 0.57f, 0.94f, 1.f}};
constexpr ImVec4 kLitColor{1.f, 0.88f, 0.47f, 1.f};
constexpr ImVec4 kOutlineColor{0.03f, 0.05f, 0.06f, 0.75f};
constexpr ImVec4 kCenterColor{0.92f, 0.92f, 0.9f, 1.f};
constexpr ImVec4 kScreenColor{0.78f, 0.8f, 0.82f, 1.f};
constexpr ImVec4 kLabelBg{0.04f, 0.07f, 0.09f, 0.88f};
constexpr ImVec4 kGuideColor{1.f, 0.88f, 0.47f, 0.55f};
constexpr ImVec4 kMarkerColor{0.85f, 0.87f, 0.88f, 0.75f};

struct GizmoFrame
{
    ScreenProjector proj_;
    std::array<v3f, 3> axes_{v3f::UnitX(), v3f::UnitY(), v3f::UnitZ()};
    v3f origin_ = v3f::Zero();
    v3f viewDir_ = -v3f::UnitZ();
    v2f originScreen_ = v2f::Zero();
    v2f offset_ = v2f::Zero();
    float axisLen_ = 1.f;
    float pixelsPerUnit_ = 1.f;
};

ImVec2 at(const GizmoFrame& f, const v2f& p)
{
    return ImVec2{p.x() + f.offset_.x(), p.y() + f.offset_.y()};
}

float wrapPi(float a)
{
    a = std::fmod(a + kPi, kTwoPi);
    if (a < 0.f)
        a += kTwoPi;
    return a - kPi;
}

float snapTo(float v, float step)
{
    return step > 0.f ? std::round(v / step) * step : v;
}

float cross2(const v2f& a, const v2f& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

ImVec4 mixColor(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t};
}

ImU32 fade(const ImVec4& base, float alphaScale)
{
    ImVec4 c = base;
    c.w *= alphaScale;
    return ImGui::GetColorU32(c);
}

float distanceToSegment(const v2f& p, const v2f& a, const v2f& b)
{
    const v2f ab = b - a;
    const float len2 = ab.squaredNorm();
    if (len2 < 1e-6f)
        return (p - a).norm();

    const float t = std::clamp((p - a).dot(ab) / len2, 0.f, 1.f);
    return (p - (a + ab * t)).norm();
}

bool closestOnAxis(const Ray& ray, const v3f& origin, const v3f& dir, float& outT)
{
    const v3f w0 = origin - ray.origin_;
    const float b = dir.dot(ray.dir_);
    const float denom = 1.f - b * b;
    if (denom < kMinAxisDenom)
        return false;

    outT = (b * ray.dir_.dot(w0) - dir.dot(w0)) / denom;
    return true;
}

bool rayPlane(const Ray& ray, const v3f& p0, const v3f& n, v3f& out)
{
    const float denom = ray.dir_.dot(n);
    if (std::abs(denom) < kMinPlaneDenom)
        return false;

    const float t = (p0 - ray.origin_).dot(n) / denom;
    if (t < 0.f)
        return false;

    out = ray.origin_ + ray.dir_ * t;
    return true;
}

void orthoBasis(const v3f& n, v3f& u, v3f& v)
{
    const v3f ref = std::abs(n.x()) < 0.9f ? v3f::UnitX() : v3f::UnitY();
    u = n.cross(ref).normalized();
    v = n.cross(u);
}

float angleOn(const v3f& p, const v3f& origin, const v3f& u, const v3f& v)
{
    const v3f d = p - origin;
    return std::atan2(d.dot(v), d.dot(u));
}

bool clipLineToRect(const v2f& p, const v2f& d, const v2f& lo, const v2f& hi, v2f& a, v2f& b)
{
    float t0 = -std::numeric_limits<float>::max();
    float t1 = std::numeric_limits<float>::max();

    for (Eigen::Index i = 0; i < 2; ++i)
    {
        if (std::abs(d[i]) < 1e-6f)
        {
            if (p[i] < lo[i] || p[i] > hi[i])
                return false;
            continue;
        }

        float ta = (lo[i] - p[i]) / d[i];
        float tb = (hi[i] - p[i]) / d[i];
        if (ta > tb)
            std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
    }

    if (t0 > t1)
        return false;

    a = p + d * t0;
    b = p + d * t1;
    return true;
}

void dashedLine(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float thickness)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.f)
        return;

    const float ux = dx / len;
    const float uy = dy / len;
    for (float t = 0.f; t < len; t += kDashLen + kDashGap)
    {
        const float e = std::min(t + kDashLen, len);
        dl->AddLine({a.x + ux * t, a.y + uy * t}, {a.x + ux * e, a.y + uy * e}, col, thickness);
    }
}

bool axisTip(const GizmoFrame& f, size_t axis, float stretch, v2f& out)
{
    const auto s = f.proj_.project(f.origin_ + f.axes_[axis] * f.axisLen_ * stretch);
    if (!s || (*s - f.originScreen_).norm() < kMinAxisPixels)
        return false;

    out = *s;
    return true;
}

bool planeQuad(const GizmoFrame& f, size_t normalAxis, std::array<v2f, 4>& out)
{
    const v3f n = f.axes_[normalAxis];
    if (std::abs(n.dot(f.viewDir_)) < kEdgeOnDot)
        return false;

    const v3f toCam = f.proj_.camPos_ - f.origin_;
    const v3f rawU = f.axes_[(normalAxis + 1) % 3];
    const v3f rawV = f.axes_[(normalAxis + 2) % 3];
    const v3f u = rawU * (toCam.dot(rawU) < 0.f ? -1.f : 1.f);
    const v3f v = rawV * (toCam.dot(rawV) < 0.f ? -1.f : 1.f);

    const float lo = kPlaneInner * f.axisLen_;
    const float hi = kPlaneOuter * f.axisLen_;
    const std::array<v3f, 4> corners{f.origin_ + u * lo + v * lo, f.origin_ + u * hi + v * lo,
                                     f.origin_ + u * hi + v * hi, f.origin_ + u * lo + v * hi};
    for (size_t i = 0; i < 4; ++i)
    {
        const auto s = f.proj_.project(corners[i]);
        if (!s)
            return false;
        out[i] = *s;
    }
    return true;
}

bool pointInQuad(const v2f& p, const std::array<v2f, 4>& q)
{
    bool pos = false;
    bool neg = false;
    for (size_t i = 0; i < 4; ++i)
    {
        const float c = cross2(q[(i + 1) % 4] - q[i], p - q[i]);
        pos = pos || c > 0.f;
        neg = neg || c < 0.f;
    }
    return !(pos && neg);
}

bool ringPoints(const GizmoFrame& f, const v3f& normal, float radius, bool splitFacing,
                RingPoints& pts, RingFront& front)
{
    v3f u = v3f::Zero();
    v3f v = v3f::Zero();
    orthoBasis(normal, u, v);

    for (size_t i = 0; i <= kRingSegments; ++i)
    {
        const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(kRingSegments);
        const v3f p = f.origin_ + (u * std::cos(a) + v * std::sin(a)) * radius;
        const auto s = f.proj_.project(p);
        if (!s)
            return false;

        pts[i] = *s;
        front[i] = !splitFacing || (f.proj_.camPos_ - p).dot(p - f.origin_) > 0.f;
    }
    return true;
}

float distanceToRing(const v2f& p, const RingPoints& pts, const RingFront& front)
{
    float best = std::numeric_limits<float>::max();
    for (size_t i = 0; i < kRingSegments; ++i)
        if (front[i] && front[i + 1])
            best = std::min(best, distanceToSegment(p, pts[i], pts[i + 1]));
    return best;
}

void strokeRing(ImDrawList* dl, const GizmoFrame& f, const RingPoints& pts, const RingFront& front,
                const ImVec4& color, float alpha, float thickness)
{
    size_t start = 0;
    while (start < kRingSegments)
    {
        size_t end = start;
        while (end < kRingSegments && front[end] == front[start])
            ++end;

        std::array<ImVec2, kRingSegments + 1> run{};
        int count = 0;
        for (size_t i = start; i <= end; ++i)
            run[static_cast<size_t>(count++)] = at(f, pts[i]);

        if (front[start])
        {
            dl->AddPolyline(run.data(), count, fade(kOutlineColor, alpha), 0,
                            thickness + kOutlineGrow * 2.f);
            dl->AddPolyline(run.data(), count, fade(color, alpha), 0, thickness);
        }
        else
        {
            dl->AddPolyline(run.data(), count, fade(color, alpha * kBackRingAlpha), 0, thickness);
        }

        start = end;
    }
}

void drawLabel(ImDrawList* dl, const ImVec2& pos, const char* text)
{
    const ImVec2 size = ImGui::CalcTextSize(text);
    dl->AddRectFilled({pos.x - 6.f, pos.y - 3.f}, {pos.x + size.x + 6.f, pos.y + size.y + 3.f},
                      ImGui::GetColorU32(kLabelBg), 3.f);
    dl->AddText(pos, ImGui::GetColorU32(kCenterColor), text);
}

void drawShaft(ImDrawList* dl, const GizmoFrame& f, const v2f& tip, const ImVec4& color,
               float alpha, float thickness, bool arrowHead)
{
    const v2f along = (tip - f.originScreen_).normalized();
    const v2f perp(-along.y(), along.x());
    const ImU32 body = fade(color, alpha);
    const ImU32 outline = fade(kOutlineColor, alpha);

    if (arrowHead)
    {
        const v2f edge = tip - along * kHeadPixels;
        const float grown = kHeadHalfWidth + kOutlineGrow;
        dl->AddLine(at(f, f.originScreen_), at(f, edge), outline, thickness + kOutlineGrow * 2.f);
        dl->AddTriangleFilled(at(f, tip + along * kOutlineGrow),
                              at(f, edge + perp * grown - along * kOutlineGrow),
                              at(f, edge - perp * grown - along * kOutlineGrow), outline);
        dl->AddLine(at(f, f.originScreen_), at(f, edge), body, thickness);
        dl->AddTriangleFilled(at(f, tip), at(f, edge + perp * kHeadHalfWidth),
                              at(f, edge - perp * kHeadHalfWidth), body);
        return;
    }

    const ImVec2 end = at(f, tip);
    const float grown = kBoxPixels + kOutlineGrow;
    dl->AddLine(at(f, f.originScreen_), end, outline, thickness + kOutlineGrow * 2.f);
    dl->AddRectFilled({end.x - grown, end.y - grown}, {end.x + grown, end.y + grown}, outline,
                      1.5f);
    dl->AddLine(at(f, f.originScreen_), end, body, thickness);
    dl->AddRectFilled({end.x - kBoxPixels, end.y - kBoxPixels},
                      {end.x + kBoxPixels, end.y + kBoxPixels}, body, 1.5f);
}

void drawGuide(ImDrawList* dl, const GizmoFrame& f, const v3f& axis)
{
    const auto tip = f.proj_.project(f.origin_ + axis * f.axisLen_);
    if (!tip)
        return;

    const v2f dir = *tip - f.originScreen_;
    if (dir.norm() < 1.f)
        return;

    v2f a = v2f::Zero();
    v2f b = v2f::Zero();
    if (!clipLineToRect(f.originScreen_, dir.normalized(), v2f::Zero(), f.proj_.frameSize_, a, b))
        return;

    dashedLine(dl, at(f, a), at(f, b), ImGui::GetColorU32(kGuideColor), 1.4f);
}

void drawOriginMarker(ImDrawList* dl, const GizmoFrame& f, const v3f& startPivot)
{
    const auto start = f.proj_.project(startPivot);
    if (!start)
        return;

    const ImU32 col = ImGui::GetColorU32(kMarkerColor);
    const ImVec2 c = at(f, *start);
    dl->AddLine({c.x - kMarkerArm, c.y - kMarkerArm}, {c.x + kMarkerArm, c.y + kMarkerArm}, col,
                1.6f);
    dl->AddLine({c.x - kMarkerArm, c.y + kMarkerArm}, {c.x + kMarkerArm, c.y - kMarkerArm}, col,
                1.6f);
    dashedLine(dl, c, at(f, f.originScreen_), col, 1.4f);
}

void drawTicks(ImDrawList* dl, const GizmoFrame& f, const v3f& axis, float step, float current)
{
    if (step <= 0.f || step * f.pixelsPerUnit_ < kTickMinSpacing)
        return;

    const v3f perpSource = std::abs(axis.dot(f.viewDir_)) > 0.9f ? f.proj_.camUp_ : f.viewDir_;
    const v3f perp = axis.cross(perpSource).normalized();
    const float base = std::round(current / step) * step;
    const ImU32 col = ImGui::GetColorU32(kGuideColor);

    for (int k = -kTickSpan; k <= kTickSpan; ++k)
    {
        const float offset = base + static_cast<float>(k) * step - current;
        const v3f p = f.origin_ + axis * offset;
        const auto mid = f.proj_.project(p);
        const auto side = f.proj_.project(p + perp * (kTickHalf / f.pixelsPerUnit_));
        if (!mid || !side)
            continue;

        const v2f arm = *side - *mid;
        dl->AddLine(at(f, *mid - arm), at(f, *mid + arm), col, 1.3f);
    }
}

void drawRingTicks(ImDrawList* dl, const GizmoFrame& f, const v3f& axis, float stepDegrees)
{
    const int count = stepDegrees > 0.f ? static_cast<int>(std::round(360.f / stepDegrees)) : 0;
    if (count < 1 || count > 180)
        return;

    v3f u = v3f::Zero();
    v3f v = v3f::Zero();
    orthoBasis(axis, u, v);
    const ImU32 col = ImGui::GetColorU32(kGuideColor);

    for (int k = 0; k < count; ++k)
    {
        const float a = kTwoPi * static_cast<float>(k) / static_cast<float>(count);
        const v3f dir = u * std::cos(a) + v * std::sin(a);
        const auto inner = f.proj_.project(f.origin_ + dir * (f.axisLen_ * 0.94f));
        const auto outer = f.proj_.project(f.origin_ + dir * (f.axisLen_ * 1.06f));
        if (!inner || !outer)
            continue;

        dl->AddLine(at(f, *inner), at(f, *outer), col, 1.3f);
    }
}
}  // namespace

bool Gizmo::draw(World& world, Engine& ctx, const Selection& selection)
{
    hovered_ = kNone;

    const auto giveUp = [this]
    {
        if (dragId_ != kNone)
            platformShowCursor(true);
        dragId_ = kNone;
        typedLen_ = 0;
        return false;
    };

    if (selection.empty() || !selection.primary().valid())
        return giveUp();

    const auto proj = screenProjector(world, ctx);
    if (!proj)
        return giveUp();

    if (dragId_ == kNone)
    {
        targets_.clear();
        for (const EntityHandle& ent : selection.all())
        {
            if (!ent.valid() || !ent.try_get<Transform_C>())
                continue;

            // A child whose ancestor is selected too would take the delta twice.
            bool nested = false;
            for (const EntityHandle& other : selection.all())
                if (!(other == ent) && other.valid() && Hierarchy_S::isDescendantOf(ent, other))
                    nested = true;
            if (nested)
                continue;

            const Transform_C& t = ent.get<Transform_C>();
            targets_.push_back(
                Target{ent, t.world().translation(), t.scale(), quatf(t.world().rotation())});
        }
    }

    if (targets_.empty())
        return giveUp();

    GizmoFrame f;
    f.proj_ = *proj;

    if (dragId_ != kNone)
    {
        f.origin_ = dragStartPivot_ + dragDelta_;
    }
    else
    {
        AABB box;
        if (pivot_ == GizmoPivot::Center)
            for (const Target& t : targets_)
                if (const auto b = entityBounds(world, ctx, t.handle_.entity_))
                    box.extend(*b);

        if (box.valid())
        {
            f.origin_ = (box.min_ + box.max_) * 0.5f;
        }
        else
        {
            for (const Target& t : targets_)
                f.origin_ += t.handle_.get<Transform_C>().world().translation();
            f.origin_ /= static_cast<float>(targets_.size());
        }
    }

    const auto originScreen = f.proj_.project(f.origin_);
    if (!originScreen)
        return giveUp();
    f.originScreen_ = *originScreen;

    const v3f toCam = f.proj_.camPos_ - f.origin_;
    if (toCam.squaredNorm() < 1e-8f)
        return giveUp();
    f.viewDir_ = -toCam.normalized();

    const auto sideScreen = f.proj_.project(f.origin_ + f.proj_.camRight_);
    if (!sideScreen)
        return giveUp();

    f.pixelsPerUnit_ = (*sideScreen - f.originScreen_).norm();
    if (f.pixelsPerUnit_ < 1e-3f)
        return giveUp();
    f.axisLen_ = kAxisPixels / f.pixelsPerUnit_;

    if (local_ || mode_ == GizmoMode::Scale)
    {
        const m3f basis = targets_.front().handle_.get<Transform_C>().world().linear();
        for (size_t i = 0; i < 3; ++i)
        {
            if (basis.col(toIndex(i)).squaredNorm() < 1e-8f)
                return giveUp();
            f.axes_[i] = basis.col(toIndex(i)).normalized();
        }
    }

    const ImVec2 vpPos = ImGui::GetMainViewport()->Pos;
    f.offset_ = v2f(vpPos.x, vpPos.y);

    InputManager& input = *ctx.inputManager_;
    const ImGuiIO& io = ImGui::GetIO();
    const v2i mousePx = input.mousePos();
    const v2f rawMouse(static_cast<float>(mousePx.x()), static_cast<float>(mousePx.y()));

    const float precision = input.down(Key::LShift) ? kPrecision : 1.f;

    if (dragId_ == kNone)
        virtualMouse_ = rawMouse;
    else
        virtualMouse_ += v2f(static_cast<float>(input.mouseDelta().x()),
                             static_cast<float>(input.mouseDelta().y())) *
                         precision;

    const v2f mouse = virtualMouse_;
    const bool pressed = input.pressed(MouseButton::Left);
    const bool held = input.down(MouseButton::Left);

    std::array<v2f, 3> tips{};
    std::array<bool, 3> hasTip{};
    std::array<RingPoints, 4> rings{};
    std::array<RingFront, 4> ringFront{};
    std::array<bool, 4> hasRing{};

    if (mode_ == GizmoMode::Rotate)
    {
        for (size_t i = 0; i < 3; ++i)
            hasRing[i] = ringPoints(f, f.axes_[i], f.axisLen_, true, rings[i], ringFront[i]);
        hasRing[3] =
            ringPoints(f, -f.viewDir_, f.axisLen_ * kRingOuterScale, false, rings[3], ringFront[3]);
    }
    else
    {
        for (size_t i = 0; i < 3; ++i)
            hasTip[i] = axisTip(f, i, 1.f, tips[i]);
    }

    if (dragId_ == kNone && !io.WantCaptureMouse)
    {
        float best = kGrabPixels;

        if (mode_ == GizmoMode::Rotate)
        {
            for (size_t i = 0; i < 4; ++i)
            {
                if (!hasRing[i])
                    continue;

                const float d = distanceToRing(mouse, rings[i], ringFront[i]);
                if (d < best)
                {
                    best = d;
                    hovered_ = i == 3 ? kScreenId : static_cast<int>(i);
                }
            }

            if (hovered_ == kNone && (mouse - f.originScreen_).norm() < kTrackballRadius)
                hovered_ = kTrackballId;
        }
        else
        {
            const float centreGrab = mode_ == GizmoMode::Scale ? kBoxPixels + 3.f : kScreenRadius;
            if ((mouse - f.originScreen_).norm() <= centreGrab)
                hovered_ = kScreenId;

            for (size_t i = 0; hovered_ == kNone && i < 3; ++i)
            {
                std::array<v2f, 4> quad{};
                if (planeQuad(f, i, quad) && pointInQuad(mouse, quad))
                    hovered_ = kPlaneFirst + static_cast<int>(i);
            }

            if (hovered_ == kNone)
                for (size_t i = 0; i < 3; ++i)
                {
                    if (!hasTip[i])
                        continue;

                    const float d = distanceToSegment(mouse, f.originScreen_, tips[i]);
                    if (d < best)
                    {
                        best = d;
                        hovered_ = static_cast<int>(i);
                    }
                }
        }
    }

    const Ray ray = rayFromScreen(world, ctx, mouse);

    const auto handleAxis = [&f](int id)
    { return id >= kScreenId ? -f.viewDir_ : f.axes_[idAxis(id)]; };

    if (hovered_ != kNone && pressed)
    {
        const v3f axis = handleAxis(hovered_);
        bool grabbed = false;

        if (hovered_ == kTrackballId)
        {
            dragPrevRaw_ = v3f(mouse.x(), mouse.y(), 0.f);
            grabbed = true;
        }
        else if (mode_ == GizmoMode::Rotate)
        {
            v3f hit = v3f::Zero();
            v3f u = v3f::Zero();
            v3f v = v3f::Zero();
            orthoBasis(axis, u, v);
            if (rayPlane(ray, f.origin_, axis, hit))
            {
                dragStartAngle_ = angleOn(hit, f.origin_, u, v);
                dragPrevRaw_ = v3f(dragStartAngle_, 0.f, 0.f);
                grabbed = true;
            }
        }
        else if (isAxis(hovered_))
        {
            float t = 0.f;
            if (closestOnAxis(ray, f.origin_, axis, t))
            {
                dragPrevRaw_ = v3f(t, 0.f, 0.f);
                grabbed = true;
            }
        }
        else if (mode_ == GizmoMode::Scale)
        {
            dragPrevRaw_ = v3f((mouse - f.originScreen_).norm() / f.pixelsPerUnit_, 0.f, 0.f);
            grabbed = true;
        }
        else
        {
            const v3f normal = isPlane(hovered_) ? f.axes_[idAxis(hovered_)] : -f.viewDir_;
            v3f hit = v3f::Zero();
            if (rayPlane(ray, f.origin_, normal, hit))
            {
                dragPrevRaw_ = hit;
                grabbed = true;
            }
        }

        if (grabbed)
        {
            dragId_ = hovered_;
            dragAxis_ = axis;
            dragStartPivot_ = f.origin_;
            dragDelta_ = v3f::Zero();
            dragAccum_ = v3f::Zero();
            dragAccumRot_ = quatf::Identity();
            typedLen_ = 0;
            platformShowCursor(false);
        }
    }

    std::array<char, 64> text{};
    bool snapping = false;
    quatf appliedRot = quatf::Identity();
    v3f appliedFactor = v3f::Ones();

    if (dragId_ != kNone)
    {
        const bool cancel = input.pressed(Key::Escape);
        if (cancel)
            for (const Target& t : targets_)
            {
                EntityHandle h = t.handle_;
                h.setPosition(t.pos_, Space::World);
                h.setRotation(t.rot_, Space::World);
                h.setLocalScale(t.scale_);
            }

        if (cancel || !held)
        {
            platformShowCursor(true);
            dragId_ = kNone;
            typedLen_ = 0;
            dragDelta_ = v3f::Zero();
        }
        else
        {
            const v3f axis = dragAxis_;
            snapping = input.down(Key::LCtrl);

            const bool acceptsTyped = mode_ != GizmoMode::Translate || isAxis(dragId_);
            if (acceptsTyped)
            {
                for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
                {
                    const ImWchar c = io.InputQueueCharacters[i];
                    const bool usable = (c >= '0' && c <= '9') || c == '.' || c == '-';
                    if (usable && typedLen_ < static_cast<int>(typed_.size()) - 1)
                        typed_[static_cast<size_t>(typedLen_++)] = static_cast<char>(c);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && typedLen_ > 0)
                    --typedLen_;
            }

            typed_[static_cast<size_t>(typedLen_)] = '\0';
            const bool typing = typedLen_ > 0;
            const float typedValue = typing ? std::strtof(typed_.data(), nullptr) : 0.f;

            if (dragId_ == kTrackballId)
            {
                // Constant gain, so the rotation keeps answering once the
                // pointer has left the disc it was grabbed in.
                const v2f moved = mouse - v2f(dragPrevRaw_.x(), dragPrevRaw_.y());
                dragPrevRaw_ = v3f(mouse.x(), mouse.y(), 0.f);

                const quatf step = quatf(angleaxisf(moved.x() * kTrackballGain, f.proj_.camUp_)) *
                                   quatf(angleaxisf(moved.y() * kTrackballGain, f.proj_.camRight_));
                if (angleaxisf(step).angle() < kMaxStepAngle)
                    dragAccumRot_ = (step * dragAccumRot_).normalized();
                appliedRot = dragAccumRot_;

                const angleaxisf shown(appliedRot);
                std::snprintf(text.data(), text.size(), "%.1f deg",
                              static_cast<double>(shown.angle() * kDegrees));
            }
            else if (mode_ == GizmoMode::Rotate)
            {
                v3f hit = v3f::Zero();
                v3f u = v3f::Zero();
                v3f v = v3f::Zero();
                orthoBasis(axis, u, v);
                if (rayPlane(ray, dragStartPivot_, axis, hit))
                {
                    const float raw = angleOn(hit, dragStartPivot_, u, v);
                    const float step = wrapPi(raw - dragPrevRaw_.x());
                    if (std::abs(step) < kMaxStepAngle)
                        dragAccum_.x() += step;
                    dragPrevRaw_.x() = raw;
                }

                float degrees = dragAccum_.x() * kDegrees;
                if (typing)
                    degrees = typedValue;
                else if (snapping)
                    degrees = snapTo(degrees, snapDegrees_);

                appliedRot = quatf(angleaxisf(degrees / kDegrees, axis));
                std::snprintf(text.data(), text.size(), typing ? "%.1f deg _" : "%.1f deg",
                              static_cast<double>(degrees));
            }
            else if (mode_ == GizmoMode::Scale)
            {
                float raw = 0.f;
                bool gotRaw = true;
                if (isAxis(dragId_))
                    gotRaw = closestOnAxis(ray, dragStartPivot_, axis, raw);
                else
                    raw = (mouse - f.originScreen_).norm() / f.pixelsPerUnit_;

                if (gotRaw)
                {
                    const float step = raw - dragPrevRaw_.x();
                    if (std::abs(step) < f.axisLen_ * kMaxStepAxes)
                        dragAccum_.x() += step;
                    dragPrevRaw_.x() = raw;
                }

                const float factor = typing ? typedValue : 1.f + dragAccum_.x() / f.axisLen_;
                const v3f startScale = targets_.front().scale_;

                const auto axisFactor = [&](size_t i)
                {
                    const Eigen::Index k = toIndex(i);
                    if (startScale[k] <= kMinScale)
                        return 1.f;

                    float s = startScale[k] * factor;
                    if (snapping && !typing)
                        s = snapTo(s, snapScale_);
                    return std::max(kMinScale, s) / startScale[k];
                };

                if (isAxis(dragId_))
                    appliedFactor[toIndex(idAxis(dragId_))] = axisFactor(idAxis(dragId_));
                else if (isPlane(dragId_))
                {
                    appliedFactor[toIndex((idAxis(dragId_) + 1) % 3)] =
                        axisFactor((idAxis(dragId_) + 1) % 3);
                    appliedFactor[toIndex((idAxis(dragId_) + 2) % 3)] =
                        axisFactor((idAxis(dragId_) + 2) % 3);
                }
                else
                    for (size_t i = 0; i < 3; ++i)
                        appliedFactor[toIndex(i)] = axisFactor(i);

                std::snprintf(text.data(), text.size(), typing ? "x %.3f _" : "x %.3f",
                              static_cast<double>(factor));
            }
            else if (isAxis(dragId_))
            {
                float raw = 0.f;
                if (closestOnAxis(ray, dragStartPivot_, axis, raw))
                {
                    const float step = raw - dragPrevRaw_.x();
                    if (std::abs(step) < f.axisLen_ * kMaxStepAxes)
                        dragAccum_.x() += step;
                    dragPrevRaw_.x() = raw;
                }

                float along = typing ? typedValue : dragAccum_.x();
                if (snapping && !typing)
                {
                    const Eigen::Index k = toIndex(idAxis(dragId_));
                    along = snapTo(dragStartPivot_[k] + along, snapUnits_) - dragStartPivot_[k];
                }

                dragDelta_ = axis * along;
                std::snprintf(text.data(), text.size(), typing ? "%.3f _" : "%.3f",
                              static_cast<double>(along));
            }
            else
            {
                const v3f normal = isPlane(dragId_) ? f.axes_[idAxis(dragId_)] : -f.viewDir_;
                v3f hit = v3f::Zero();
                if (rayPlane(ray, dragStartPivot_, normal, hit))
                {
                    const v3f step = hit - dragPrevRaw_;
                    if (step.norm() < f.axisLen_ * kMaxStepAxes)
                        dragAccum_ += step;
                    dragPrevRaw_ = hit;
                }

                dragDelta_ = dragAccum_;
                if (snapping)
                    for (Eigen::Index k = 0; k < 3; ++k)
                        if (!isPlane(dragId_) || k != toIndex(idAxis(dragId_)))
                            dragDelta_[k] = snapTo(dragStartPivot_[k] + dragAccum_[k], snapUnits_) -
                                            dragStartPivot_[k];

                std::snprintf(text.data(), text.size(), "%.2f  %.2f  %.2f",
                              static_cast<double>(dragDelta_.x()),
                              static_cast<double>(dragDelta_.y()),
                              static_cast<double>(dragDelta_.z()));
            }

            const m3f basis = (m3f() << f.axes_[0], f.axes_[1], f.axes_[2]).finished();
            for (const Target& t : targets_)
            {
                EntityHandle h = t.handle_;
                const v3f offset = t.pos_ - dragStartPivot_;

                if (mode_ == GizmoMode::Translate)
                {
                    h.setPosition(t.pos_ + dragDelta_, Space::World);
                }
                else if (mode_ == GizmoMode::Rotate)
                {
                    h.setRotation((appliedRot * t.rot_).normalized(), Space::World);
                    h.setPosition(dragStartPivot_ + appliedRot * offset, Space::World);
                }
                else
                {
                    h.setLocalScale(t.scale_.cwiseProduct(appliedFactor));
                    h.setPosition(dragStartPivot_ + basis * appliedFactor.cwiseProduct(
                                                                basis.transpose() * offset),
                                  Space::World);
                }
            }

            if (mode_ == GizmoMode::Translate)
            {
                f.origin_ = dragStartPivot_ + dragDelta_;
                if (const auto moved = f.proj_.project(f.origin_))
                    f.originScreen_ = *moved;
            }
        }
    }

    const float dt = std::max(io.DeltaTime, 1e-4f);
    const float blend = 1.f - std::exp(-dt / kGlowTau);
    const int lit = dragId_ != kNone ? dragId_ : hovered_;
    for (size_t i = 0; i < kHandleCount; ++i)
        glow_[i] += ((lit == static_cast<int>(i) ? 1.f : 0.f) - glow_[i]) * blend;
    dimFade_ += ((lit != kNone ? 1.f : 0.f) - dimFade_) * blend;

    const auto alphaOf = [this](size_t i)
    { return 1.f - dimFade_ * (1.f - glow_[i]) * (1.f - kDimAlpha); };
    const auto colorAt = [this](size_t i, const ImVec4& base)
    { return mixColor(base, kLitColor, glow_[i]); };
    const auto thicknessAt = [this](size_t i)
    { return kShaftThickness + (kShaftLit - kShaftThickness) * glow_[i]; };

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    if (dragId_ != kNone && dragId_ != kTrackballId)
    {
        if (mode_ == GizmoMode::Rotate)
        {
            if (snapping)
                drawRingTicks(dl, f, dragAxis_, snapDegrees_);
        }
        else if (isAxis(dragId_))
        {
            drawGuide(dl, f, dragAxis_);
            if (snapping)
            {
                const bool scaling = mode_ == GizmoMode::Scale;
                drawTicks(dl, f, dragAxis_, scaling ? snapScale_ * f.axisLen_ : snapUnits_,
                          scaling ? 0.f : f.origin_[toIndex(idAxis(dragId_))]);
            }
        }
        else if (isPlane(dragId_))
        {
            drawGuide(dl, f, f.axes_[(idAxis(dragId_) + 1) % 3]);
            drawGuide(dl, f, f.axes_[(idAxis(dragId_) + 2) % 3]);
        }

        if (mode_ == GizmoMode::Translate)
            drawOriginMarker(dl, f, dragStartPivot_);
    }

    if (mode_ == GizmoMode::Rotate)
    {
        if (glow_[static_cast<size_t>(kTrackballId)] > 0.01f)
            dl->AddCircleFilled(
                at(f, f.originScreen_), kTrackballRadius,
                fade(kScreenColor, 0.07f * glow_[static_cast<size_t>(kTrackballId)]));

        const size_t centre = static_cast<size_t>(kScreenId);
        if (hasRing[3])
            strokeRing(dl, f, rings[3], ringFront[3], colorAt(centre, kScreenColor),
                       alphaOf(centre), thicknessAt(centre));

        for (size_t i = 0; i < 3; ++i)
            if (hasRing[i])
                strokeRing(dl, f, rings[i], ringFront[i], colorAt(i, kAxisColors[i]), alphaOf(i),
                           thicknessAt(i));

        if (dragId_ != kNone && dragId_ != kTrackballId)
        {
            v3f u = v3f::Zero();
            v3f v = v3f::Zero();
            orthoBasis(dragAxis_, u, v);

            const angleaxisf aa(appliedRot);
            const float signed_ = aa.axis().dot(dragAxis_) < 0.f ? -aa.angle() : aa.angle();
            const float sweep = std::clamp(signed_, -kTwoPi, kTwoPi);
            const int steps = std::max(2, static_cast<int>(std::abs(sweep) / kTwoPi * 90.f) + 2);

            std::array<ImVec2, 96> arc{};
            int count = 0;
            arc[static_cast<size_t>(count++)] = at(f, f.originScreen_);
            bool ok = true;
            for (int k = 0; k <= steps && ok; ++k)
            {
                const float a =
                    dragStartAngle_ + sweep * static_cast<float>(k) / static_cast<float>(steps);
                const auto s =
                    f.proj_.project(f.origin_ + (u * std::cos(a) + v * std::sin(a)) * f.axisLen_);
                if (!s)
                    ok = false;
                else
                    arc[static_cast<size_t>(count++)] = at(f, *s);
            }

            if (ok && count > 2)
            {
                dl->AddConvexPolyFilled(arc.data(), count, fade(kLitColor, 0.22f));
                dl->AddLine(arc[0], arc[static_cast<size_t>(count - 1)], fade(kLitColor, 0.8f),
                            1.6f);
            }
        }

        dl->AddCircleFilled(at(f, f.originScreen_), kCenterRadius + kOutlineGrow,
                            ImGui::GetColorU32(kOutlineColor));
        dl->AddCircleFilled(at(f, f.originScreen_), kCenterRadius,
                            ImGui::GetColorU32(kCenterColor));
    }
    else
    {
        for (size_t i = 0; i < 3; ++i)
        {
            std::array<v2f, 4> quad{};
            if (!planeQuad(f, i, quad))
                continue;

            const size_t id = static_cast<size_t>(kPlaneFirst) + i;
            const std::array<ImVec2, 4> pts{at(f, quad[0]), at(f, quad[1]), at(f, quad[2]),
                                            at(f, quad[3])};
            const ImVec4 base = colorAt(id, kAxisColors[i]);
            dl->AddConvexPolyFilled(pts.data(), 4, fade(base, alphaOf(id) * kPlaneFillAlpha));
            dl->AddPolyline(pts.data(), 4, fade(base, alphaOf(id)), ImDrawFlags_Closed, 1.6f);
        }

        std::array<size_t, 3> order{0, 1, 2};
        std::sort(order.begin(), order.end(),
                  [&f](size_t a, size_t b)
                  {
                      return (f.origin_ + f.axes_[a] * f.axisLen_ - f.proj_.camPos_).squaredNorm() >
                             (f.origin_ + f.axes_[b] * f.axisLen_ - f.proj_.camPos_).squaredNorm();
                  });

        for (const size_t i : order)
        {
            const float stretch = mode_ == GizmoMode::Scale ? appliedFactor[toIndex(i)] : 1.f;
            v2f tip = v2f::Zero();
            if (!axisTip(f, i, stretch, tip))
                continue;

            drawShaft(dl, f, tip, colorAt(i, kAxisColors[i]), alphaOf(i), thicknessAt(i),
                      mode_ == GizmoMode::Translate);
        }

        const size_t centre = static_cast<size_t>(kScreenId);
        const ImVec2 c = at(f, f.originScreen_);
        if (mode_ == GizmoMode::Scale)
        {
            const float half = kBoxPixels + 1.f;
            dl->AddRectFilled({c.x - half - kOutlineGrow, c.y - half - kOutlineGrow},
                              {c.x + half + kOutlineGrow, c.y + half + kOutlineGrow},
                              fade(kOutlineColor, alphaOf(centre)), 2.f);
            dl->AddRectFilled({c.x - half, c.y - half}, {c.x + half, c.y + half},
                              fade(colorAt(centre, kCenterColor), alphaOf(centre)), 2.f);
        }
        else
        {
            dl->AddCircle(c, kScreenRadius, fade(kOutlineColor, alphaOf(centre)), 0, 3.6f);
            dl->AddCircle(c, kScreenRadius, fade(colorAt(centre, kScreenColor), alphaOf(centre)), 0,
                          1.6f);
            dl->AddCircleFilled(c, kCenterRadius + kOutlineGrow,
                                fade(kOutlineColor, alphaOf(centre)));
            dl->AddCircleFilled(c, kCenterRadius, fade(kCenterColor, alphaOf(centre)));
        }
    }

    if (dragId_ != kNone && text[0] != '\0')
    {
        const ImVec2 anchor = at(f, f.originScreen_);
        drawLabel(dl, ImVec2{anchor.x + 26.f, anchor.y - 34.f}, text.data());
    }

    if (dragId_ != kNone)
    {
        v2i warp = mousePx;
        bool wrap = false;
        for (Eigen::Index i = 0; i < 2; ++i)
        {
            const float span = f.proj_.frameSize_[i];
            if (rawMouse[i] < kWrapMargin)
            {
                warp[i] = static_cast<int>(span - kWrapMargin);
                wrap = true;
            }
            else if (rawMouse[i] > span - kWrapMargin)
            {
                warp[i] = static_cast<int>(kWrapMargin);
                wrap = true;
            }
        }
        if (wrap)
            platformSetCursorPos(ctx.nativeWindow(), warp.x(), warp.y());
    }

    return dragId_ != kNone || hovered_ != kNone;
}

}  // namespace batap
