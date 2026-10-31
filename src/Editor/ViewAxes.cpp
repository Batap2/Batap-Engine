#include "ViewAxes.h"

#include "Components/Transform_C.h"
#include "Engine.h"
#include "HandleColors.h"
#include "InputManager.h"
#include "Instance/InstanceManager.h"
#include "Spatial/SpatialIndex.h"
#include "Systems/FreeCamController_S.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace batap
{
namespace
{
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kQuarterTurn = kPi * 0.5f;

constexpr float kMargin = 10.f;
constexpr float kArm = 32.f;
constexpr float kBall = 9.f;
constexpr float kBackBall = 6.5f;
constexpr float kExtent = kArm + kBall + 3.f;
constexpr float kGrabSlack = 2.f;
constexpr float kStemThickness = 2.f;
constexpr float kRingThickness = 1.5f;
constexpr float kBackFill = 0.3f;
constexpr float kGlowTau = 0.045f;

constexpr float kAlignSeconds = 0.25f;
constexpr float kAligned = 0.9999f;
constexpr float kDefaultFocus = 10.f;
constexpr float kWheelZoom = 1.15f;

constexpr int kNone = -1;
constexpr size_t kBallCount = 6;
constexpr size_t kDiscId = kBallCount;

constexpr ImVec4 kLabelColor{0.05f, 0.06f, 0.07f, 1.f};
constexpr ImVec4 kDiscColor{0.78f, 0.8f, 0.82f, 0.16f};
constexpr std::array<const char*, kBallCount> kLabels{"X", "Y", "Z", "-X", "-Y", "-Z"};

constexpr ImGuiWindowFlags kWindowFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;

v3f ballDir(size_t id)
{
    const v3f axis = v3f::Unit(static_cast<Eigen::Index>(id % 3));
    return id < 3 ? axis : -axis;
}

float wrapPi(float a)
{
    return std::remainder(a, 2.f * kPi);
}
}  // namespace

void ViewAxes::draw(World& world, Engine& ctx, EntityHandle cam, ImVec2 viewMin, ImVec2 viewMax)
{
    auto* ctrl = cam.valid() ? cam.try_get<FreeCamController_C>() : nullptr;
    if (!ctrl)
        return;

    animate(ctx, cam, *ctrl);
    navigateOrtho(world, ctx, cam, viewMin, viewMax);

    constexpr float size = kExtent * 2.f;
    if (viewMax.x - viewMin.x < size + kMargin * 2.f ||
        viewMax.y - viewMin.y < size + kMargin * 2.f)
        return;

    const ImVec2 corner{viewMax.x - kMargin - size, viewMin.y + kMargin};
    ImGui::SetNextWindowPos(corner, ImGuiCond_Always);
    ImGui::SetNextWindowSize({size, size}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::Begin("##viewAxes", nullptr, kWindowFlags);
    ImGui::PopStyleVar();

    const bool released = ImGui::InvisibleButton("##axes", {size, size});
    const bool hovered = ImGui::IsItemHovered();

    // The transform only takes yaw and pitch in FreeCamController_S, later
    // this frame.
    const quatf toView = freeCamRotation(*ctrl).conjugate();
    const v2f centre{corner.x + kExtent, corner.y + kExtent};
    std::array<v2f, kBallCount> spots{};
    std::array<float, kBallCount> depth{};
    std::array<size_t, kBallCount> order{};
    for (size_t i = 0; i < kBallCount; ++i)
    {
        const v3f d = toView * ballDir(i);
        spots[i] = centre + v2f{d.x(), -d.y()} * kArm;
        depth[i] = d.z();
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return depth[a] < depth[b]; });

    const auto radiusOf = [](size_t id) { return id < 3 ? kBall : kBackBall; };

    int hit = kNone;
    if (hovered)
    {
        const ImVec2 m = ImGui::GetIO().MousePos;
        for (auto it = order.rbegin(); it != order.rend() && hit == kNone; ++it)
            if ((v2f{m.x, m.y} - spots[*it]).norm() <= radiusOf(*it) + kGrabSlack)
                hit = static_cast<int>(*it);
    }

    if (hit != kNone)
    {
        const size_t id = static_cast<size_t>(hit);
        if (released)
            align(world, ctx, cam, *ctrl, -ballDir(id));
        else if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("View from %s", kLabels[id]);
    }

    const float blend = 1.f - std::exp(-std::max(ImGui::GetIO().DeltaTime, 1e-4f) / kGlowTau);
    for (size_t i = 0; i < kBallCount; ++i)
        glow_[i] += ((hit == static_cast<int>(i) ? 1.f : 0.f) - glow_[i]) * blend;
    glow_[kDiscId] += ((hovered ? 1.f : 0.f) - glow_[kDiscId]) * blend;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c{centre.x(), centre.y()};
    if (glow_[kDiscId] > 0.01f)
        dl->AddCircleFilled(c, kExtent - 1.f, fade(kDiscColor, glow_[kDiscId]));

    for (const size_t id : order)
    {
        const ImVec4 color = mixColor(kAxisColors[id % 3], kLitColor, glow_[id]);
        const ImVec2 p{spots[id].x(), spots[id].y()};
        if (id >= 3)
        {
            dl->AddCircleFilled(p, kBackBall,
                                fade(color, kBackFill + (1.f - kBackFill) * glow_[id]));
            dl->AddCircle(p, kBackBall, fade(color, 1.f), 0, kRingThickness);
            continue;
        }
        dl->AddLine(c, p, fade(color, 1.f), kStemThickness);
        dl->AddCircleFilled(p, kBall, fade(color, 1.f));
        const ImVec2 text = ImGui::CalcTextSize(kLabels[id]);
        dl->AddText({p.x - text.x * 0.5f, p.y - text.y * 0.5f}, fade(kLabelColor, 1.f),
                    kLabels[id]);
    }

    ImGui::End();
}

void ViewAxes::align(World& world, Engine& ctx, EntityHandle cam, FreeCamController_C& ctrl,
                     const v3f& look)
{
    const v3f pos = cam.get<Transform_C>().pos();
    const v3f forward = freeCamRotation(ctrl) * -v3f::UnitZ();
    const v3f dir = forward.dot(look) > kAligned ? v3f(-look) : look;

    Camera_C& view = cam.get<Camera_C>();
    if (view.ortho())
        distance_ = ctrl.orthoFocus_;
    else
    {
        const ViewRect rect = ctx.viewRect();
        const v2f screenCentre = rect.origin_.cast<float>() + rect.size_.cast<float>() * 0.5f;
        const Ray ray = rayFromScreen(world, ctx, screenCentre);
        float depth = std::numeric_limits<float>::infinity();
        if (const RayHit body = world.raycastPhysics(ray); body.hit())
            depth = (body.point_ - pos).dot(forward);
        // The index only knows bounds: its hit point is on the box, often in
        // the air in front of the mesh.
        if (const RayHit box = world.spatialIndex().raycast(ray); box.hit())
            if (const auto bounds = entityBounds(world, ctx, box.entity_))
                depth = std::min(depth, (bounds->center() - pos).dot(forward));
        distance_ = std::isfinite(depth) && depth > 0.f ? depth : kDefaultFocus;
        enterFreeCamOrtho(ctrl, view, distance_);
        world.instances().markDirty<Camera_C>(cam);
    }
    pivot_ = pos + forward * distance_;

    const v3f local = ctrl.frame_.conjugate() * dir;
    fromYaw_ = ctrl.yaw_;
    fromPitch_ = ctrl.pitch_;
    if (std::abs(local.y()) > kAligned)
    {
        toYaw_ = std::round(ctrl.yaw_ / kQuarterTurn) * kQuarterTurn;
        toPitch_ = std::copysign(kQuarterTurn, local.y());
    }
    else
    {
        toYaw_ = fromYaw_ + wrapPi(std::atan2(-local.x(), -local.z()) - fromYaw_);
        toPitch_ = std::asin(std::clamp(local.y(), -1.f, 1.f));
    }
    progress_ = 0.f;
}

void ViewAxes::animate(Engine& ctx, EntityHandle cam, FreeCamController_C& ctrl)
{
    if (progress_ >= 1.f)
        return;
    if (ctx.inputManager_->down(MouseButton::Right))
    {
        progress_ = 1.f;
        return;
    }

    progress_ = std::min(progress_ + ImGui::GetIO().DeltaTime / kAlignSeconds, 1.f);
    const float s = progress_ * progress_ * (3.f - 2.f * progress_);
    ctrl.yaw_ = fromYaw_ + (toYaw_ - fromYaw_) * s;
    ctrl.pitch_ = fromPitch_ + (toPitch_ - fromPitch_) * s;
    cam.setLocalPosition(pivot_ + freeCamRotation(ctrl) * v3f::UnitZ() * distance_);
}

void ViewAxes::navigateOrtho(World& world, Engine& ctx, EntityHandle cam, ImVec2 viewMin,
                             ImVec2 viewMax)
{
    Camera_C& view = cam.get<Camera_C>();
    const InputManager& input = *ctx.inputManager_;
    if (!view.ortho() || !input.down(MouseButton::Middle))
        panning_ = false;
    if (!view.ortho())
        return;

    const bool overView = !ImGui::GetIO().WantCaptureMouse &&
                          ImGui::IsMouseHoveringRect(viewMin, viewMax, false);
    if (overView && input.pressed(MouseButton::Middle))
        panning_ = true;

    if (panning_)
    {
        const float unitsPerPixel =
            view.orthoHeight_ / static_cast<float>(std::max(ctx.viewRect().size_.y(), 1));
        const v2f d = input.mouseDelta().cast<float>();
        cam.translate(v3f{-d.x(), d.y(), 0.f} * unitsPerPixel, Space::Local);
    }

    const float wheel = input.wheel();
    if (!overView || wheel == 0.f)
        return;

    const v3f pos = cam.get<Transform_C>().pos();
    const v3f forward = cam.get<Transform_C>().rot() * -v3f::UnitZ();
    v3f across = rayFromScreen(world, ctx, input.mousePos()).origin_ - pos;
    across -= forward * across.dot(forward);

    const float height = std::clamp(view.orthoHeight_ * std::pow(kWheelZoom, -wheel),
                                    kFreeCamOrthoMinHeight, kOrthoDepth);
    const float scale = height / view.orthoHeight_;
    view.orthoHeight_ = height;
    cam.translate(across * (1.f - scale), Space::World);
    world.instances().markDirty<Camera_C>(cam);
}
}  // namespace batap
