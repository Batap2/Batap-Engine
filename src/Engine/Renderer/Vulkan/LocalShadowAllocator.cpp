#include "Renderer/Vulkan/LocalShadowAllocator.h"

#include "Components/Camera_C.h"
#include "Components/PointLight_C.h"
#include "Components/SpotLight_C.h"
#include "Components/Transform_C.h"
#include "Instance/InstanceManager.h"
#include "Renderer/EngineConfig.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

namespace batap
{
namespace
{
struct CubeFace
{
    float forward_[3];
    float up_[3];
};

constexpr std::array<CubeFace, 6> kCubeFaces{{
    {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},
    {{-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},
    {{0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}},
    {{0.f, -1.f, 0.f}, {0.f, 0.f, -1.f}},
    {{0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}},
    {{0.f, 0.f, -1.f}, {0.f, 1.f, 0.f}},
}};

// A view is drawn a few texels wider than the angle it owns. A receiver sitting
// on a face boundary otherwise lands on the tile's outermost texel, where
// neither the PCF clamp nor the normal offset can still reach the caster, and
// light leaks in a hairline along the cube's edges. In texels, so it follows
// the tile.
constexpr float kFaceGuardTexels = 4.f;

constexpr float kZNear = 0.05f;

float guardedFov(float fov, uint32_t tileSize)
{
    const float half = static_cast<float>(tileSize) * 0.5f;
    return 2.f * std::atan(std::tan(fov * 0.5f) * (half + kFaceGuardTexels) / half);
}

m4f axisViewProj(const v3f& eye, const v3f& forward, const v3f& up, float fov, float znear,
                 float zfar)
{
    const v3f zaxis = -forward;
    const v3f xaxis = up.cross(zaxis).normalized();
    const v3f yaxis = zaxis.cross(xaxis);

    m4f view = m4f::Identity();
    view.block<1, 3>(0, 0) = xaxis.transpose();
    view.block<1, 3>(1, 0) = yaxis.transpose();
    view.block<1, 3>(2, 0) = zaxis.transpose();
    view(0, 3) = -xaxis.dot(eye);
    view(1, 3) = -yaxis.dot(eye);
    view(2, 3) = -zaxis.dot(eye);

    const float f = 1.f / std::tan(fov * 0.5f);
    const float nf = 1.f / (znear - zfar);

    m4f proj = m4f::Zero();
    proj(0, 0) = f;
    proj(1, 1) = f;
    proj(2, 2) = zfar * nf;
    proj(2, 3) = zfar * znear * nf;
    proj(3, 2) = -1.f;

    return m4f{proj * view};
}

using Frustum = std::array<v4f, 6>;

// Gribb-Hartmann, for a [0, 1] clip depth.
Frustum frustumOf(const m4f& viewProj)
{
    const v4f r0 = viewProj.row(0).transpose();
    const v4f r1 = viewProj.row(1).transpose();
    const v4f r2 = viewProj.row(2).transpose();
    const v4f r3 = viewProj.row(3).transpose();
    Frustum planes{r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
    for (v4f& plane : planes)
        plane /= plane.head<3>().norm();
    return planes;
}

float distanceTo(const v4f& plane, const v3f& p)
{
    return plane.head<3>().dot(p) + plane.w();
}

bool sphereOutside(const Frustum& frustum, const v3f& centre, float radius)
{
    return std::ranges::any_of(frustum, [&](const v4f& plane)
                               { return distanceTo(plane, centre) < -radius; });
}

// The view's pyramid cut at range along its axis, which holds the part of the
// bubble the view covers: out when its five corners are behind one plane.
bool pyramidOutside(const Frustum& frustum, const v3f& apex, const v3f& forward, const v3f& up,
                    float tanHalfFov, float range)
{
    const v3f side = up.cross(-forward).normalized();
    const v3f top = (-forward).cross(side);
    const v3f base = apex + forward * range;
    const float half = tanHalfFov * range;
    const std::array<v3f, 5> corners{apex, base + (side + top) * half, base + (side - top) * half,
                                     base - (side - top) * half, base - (side + top) * half};
    for (const v4f& plane : frustum)
        if (std::ranges::all_of(corners, [&](const v3f& c) { return distanceTo(plane, c) < 0.f; }))
            return true;
    return false;
}

uint32_t tileClass(float res, uint32_t last)
{
    const float band = std::numbers::sqrt2_v<float> * LocalClassHysteresis;
    const float lastf = static_cast<float>(last);
    if (last != 0 && res > lastf / band && res < lastf * band)
        return last;
    const float nearest = std::exp2(std::round(std::log2(res)));
    return static_cast<uint32_t>(
        std::clamp(nearest, static_cast<float>(LocalTileMin), static_cast<float>(LocalTileMax)));
}

// A Morton code's even bits, packed: x from the code, y from the code shifted by one.
uint32_t evenBits(uint32_t k)
{
    k &= 0x55555555u;
    k = (k | (k >> 1)) & 0x33333333u;
    k = (k | (k >> 2)) & 0x0F0F0F0Fu;
    k = (k | (k >> 4)) & 0x00FF00FFu;
    k = (k | (k >> 8)) & 0x0000FFFFu;
    return k;
}
}  // namespace

void LocalShadowAllocator::allocate(entt::registry& reg, GPUInstanceManager& instances,
                                    entt::entity camera, v2i frameSize,
                                    std::vector<ShadowView>& views)
{
    views.clear();
    candidates_.clear();
    tiles_.clear();
    nextHistory_.clear();
    if (frameSize.x() <= 0 || frameSize.y() <= 0)
        return;

    const Camera_C& cam = reg.get<Camera_C>(camera);
    const transform camWorld = reg.get<Transform_C>(camera).world();
    const float height = static_cast<float>(frameSize.y());
    const float aspect = static_cast<float>(frameSize.x()) / height;
    const Frustum frustum = frustumOf(m4f{cam.make_proj(aspect) * cam.make_view(camWorld)});
    const v3f camPos = camWorld.translation();
    const float pixelsPerTan = height / std::tan(cam.fov_ * 0.5f);
    constexpr float fadeLow = static_cast<float>(LocalTileFade);
    constexpr float fadeHigh = static_cast<float>(LocalTileMin);

    auto& lights = instances.pool<LightInstance>();

    auto consider = [&](entt::entity e, const v3f& eye, float radius, float fov,
                        uint32_t viewTotal) -> Candidate*
    {
        const GPUInstanceID id = lights.getGPUIndex(EntityHandle{&reg, e});
        if (!id.valid() || radius <= 0.f)
            return nullptr;

        const float d = std::max((eye - camPos).norm(), radius);
        const float res = radius * std::tan(fov * 0.5f) * pixelsPerTan / d;
        if (res <= fadeLow)
            return nullptr;

        const auto it = history_.find(e);
        const History past = it == history_.end() ? History{} : it->second;

        Candidate& c = candidates_.emplace_back();
        c.entity_ = e;
        c.eye_ = eye;
        c.zfar_ = std::max(radius, kZNear + 1.f);
        c.fov_ = fov;
        c.strength_ = std::min((res - fadeLow) / (fadeHigh - fadeLow), 1.f);
        c.priority_ = (radius / d) * (radius / d);
        c.shrinkPriority_ = past.shrunk_ ? c.priority_ / LocalClassHysteresis : c.priority_;
        c.requested_ = tileClass(res, past.requested_);
        c.size_ = c.requested_;
        c.firstEntry_ = id * MaxShadowViewsPerLight;
        c.viewTotal_ = viewTotal;
        return &c;
    };

    for (auto [e, light, trans] : reg.view<PointLight_C, Transform_C>().each())
    {
        if (!light.castShadows_)
            continue;
        const v3f eye = trans.world().translation();
        Candidate* c = consider(e, eye, light.radius_, std::numbers::pi_v<float> * 0.5f,
                                static_cast<uint32_t>(kCubeFaces.size()));
        if (!c || sphereOutside(frustum, eye, light.radius_))
            continue;
        for (uint32_t face = 0; face < kCubeFaces.size(); ++face)
        {
            const CubeFace& fc = kCubeFaces[face];
            const v3f forward{fc.forward_[0], fc.forward_[1], fc.forward_[2]};
            const v3f up{fc.up_[0], fc.up_[1], fc.up_[2]};
            // The shader picks the face from the receiver's own position, which
            // is visible: a face outside the frustum is never read.
            if (!pyramidOutside(frustum, eye, forward, up, 1.f, c->zfar_))
                c->views_[c->viewCount_++] = {forward, up, c->firstEntry_ + face};
        }
    }

    for (auto [e, spot, trans] : reg.view<SpotLight_C, Transform_C>().each())
    {
        if (!spot.castShadows_)
            continue;
        const v3f eye = trans.world().translation();
        const v3f forward = -trans.world().linear().col(2).normalized();
        const v3f up = std::abs(forward.y()) > 0.99f ? v3f{0.f, 0.f, 1.f} : v3f{0.f, 1.f, 0.f};
        Candidate* c = consider(e, eye, spot.radius_, spot.outerAngle_, 1);
        if (c && !sphereOutside(frustum, eye, spot.radius_) &&
            !pyramidOutside(frustum, eye, forward, up, std::tan(spot.outerAngle_ * 0.5f), c->zfar_))
            c->views_[c->viewCount_++] = {forward, up, c->firstEntry_};
    }

    std::ranges::stable_sort(candidates_, std::ranges::greater{}, &Candidate::priority_);
    constexpr uint64_t budget = uint64_t{LocalAtlasSize} * LocalAtlasSize;
    auto cost = [](const Candidate& c) { return uint64_t{c.viewTotal_} * c.size_ * c.size_; };
    uint64_t total = 0;
    for (const Candidate& c : candidates_)
        total += cost(c);

    // Over budget, the largest tiles halve first, the least important light
    // among them first: a shadow is only lost once every light is down to
    // LocalTileMin.
    while (total > budget)
    {
        Candidate* victim = nullptr;
        for (Candidate& c : candidates_)
            if (c.size_ > LocalTileMin &&
                (!victim || c.size_ > victim->size_ ||
                 (c.size_ == victim->size_ && c.shrinkPriority_ <= victim->shrinkPriority_)))
                victim = &c;
        if (!victim)
            break;
        total -= cost(*victim) - cost(*victim) / 4;
        victim->size_ /= 2;
    }
    for (const Candidate& c : candidates_)
        nextHistory_[c.entity_] = {c.requested_, c.size_ < c.requested_};
    std::swap(history_, nextHistory_);

    // Still over: skipped rather than ending the list, the ones after may fit.
    uint64_t used = 0;
    for (const Candidate& c : candidates_)
    {
        if (used + cost(c) > budget)
            continue;
        used += cost(c);
        for (uint32_t v = 0; v < c.viewCount_; ++v)
            tiles_.push_back({&c, &c.views_[v]});
    }

    // Largest first, each tile at the running total of those before it, read
    // as a Morton code: the total is a multiple of the tile's own area, so the
    // tile lands on an aligned square and the budget is exactly what fits.
    std::ranges::stable_sort(tiles_, std::ranges::greater{},
                             [](const Tile& t) { return t.light_->size_; });
    uint32_t offset = 0;
    for (const Tile& t : tiles_)
    {
        const Candidate& c = *t.light_;
        const uint32_t cells = c.size_ / LocalTileMin;
        const float fov = guardedFov(c.fov_, c.size_);

        ShadowView& view = views.emplace_back();
        view.viewProj_ =
            axisViewProj(c.eye_, t.view_->forward_, t.view_->up_, fov, kZNear, c.zfar_);
        view.texelWorld_ = 2.f * std::tan(fov * 0.5f) / static_cast<float>(c.size_);
        view.x_ = evenBits(offset) * LocalTileMin;
        view.y_ = evenBits(offset >> 1) * LocalTileMin;
        view.size_ = c.size_;
        view.entry_ = t.view_->entry_;
        view.strength_ = c.strength_;
        offset += cells * cells;
    }
}
}  // namespace batap
