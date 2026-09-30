#include "LightGizmos.h"

#include "Components/DirectionalLight_C.h"
#include "Components/PointLight_C.h"
#include "Components/RectLight_C.h"
#include "Components/SpotLight_C.h"
#include "Components/Transform_C.h"
#include "Renderer/DebugDraw.h"
#include "Selection.h"
#include "World.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace batap
{
namespace
{
constexpr int kCircleSegments = 48;

void circle(DebugDraw& dbg, const v3f& center, const v3f& u, const v3f& v, float radius,
            const col3& color)
{
    v3f prev = center + u * radius;
    for (int i = 1; i <= kCircleSegments; ++i)
    {
        const float a = 2.f * std::numbers::pi_v<float> * static_cast<float>(i) / kCircleSegments;
        const v3f next = center + (u * std::cos(a) + v * std::sin(a)) * radius;
        dbg.line(prev, next, color);
        prev = next;
    }
}

// The cone of a spot as the shading loop sees it: every point within
// radius_ of the light and within half the angle of its axis, capped by
// the sphere.
void cone(DebugDraw& dbg, const v3f& apex, const v3f& axis, const v3f& u, const v3f& v,
          float range, float fullAngle, const col3& color, bool spokes)
{
    const float half = std::min(fullAngle * 0.5f, std::numbers::pi_v<float> * 0.5f);
    const v3f center = apex + axis * (range * std::cos(half));
    const float radius = range * std::sin(half);
    circle(dbg, center, u, v, radius, color);
    if (!spokes)
        return;
    for (int i = 0; i < 4; ++i)
    {
        const float a = std::numbers::pi_v<float> * 0.5f * static_cast<float>(i);
        dbg.line(apex, center + (u * std::cos(a) + v * std::sin(a)) * radius, color);
    }
}

void rectangle(DebugDraw& dbg, const v3f& center, const v3f& halfU, const v3f& halfV,
               const col3& color)
{
    const std::array<v3f, 4> c{center + halfU + halfV, center - halfU + halfV,
                               center - halfU - halfV, center + halfU - halfV};
    for (size_t i = 0; i < c.size(); ++i)
        dbg.line(c[i], c[(i + 1) % c.size()], color);
}

col3 dimmed(const col3& c)
{
    return c * 0.5f;
}

}  // namespace

float sunArrowLength(World& world, const v3f& pos)
{
    const entt::entity cam = world.renderCamera();
    const auto* tc = cam == entt::null ? nullptr : world.registry_.try_get<Transform_C>(cam);
    return 0.25f * (tc ? (tc->world().translation() - pos).norm() : 10.f);
}

void drawLightGizmos(World& world, const Selection& selection)
{
    DebugDraw& dbg = world.debugOverlay();
    auto& reg = world.registry_;
    const col3 color = colors::yellow;

    for (const EntityHandle& ent : selection.all())
    {
        const auto* tc = reg.try_get<Transform_C>(ent.entity_);
        if (!tc)
            continue;
        const transform xf = tc->world();
        const v3f pos = xf.translation();
        const v3f x = xf.linear().col(0).normalized();
        const v3f y = xf.linear().col(1).normalized();
        const v3f fwd = -xf.linear().col(2).normalized();

        if (const auto* point = reg.try_get<PointLight_C>(ent.entity_))
            dbg.sphere(pos, point->radius_, color);

        if (const auto* spot = reg.try_get<SpotLight_C>(ent.entity_))
        {
            cone(dbg, pos, fwd, x, y, spot->radius_, spot->outerAngle_, color, true);
            cone(dbg, pos, fwd, x, y, spot->radius_, spot->innerAngle_, dimmed(color), false);
        }

        if (const auto* rect = reg.try_get<RectLight_C>(ent.entity_))
        {
            const v3f halfU = x * (rect->width_ * 0.5f);
            const v3f halfV = y * (rect->height_ * 0.5f);
            rectangle(dbg, pos, halfU, halfV, color);
            dbg.arrow(pos, pos + fwd * std::max(rect->width_, rect->height_) * 0.5f, color);

            // The volume the barn doors let through, out to the range: the
            // face grown by range * tan(half spread) along each axis.
            const float half = rect->spreadAngle_ * 0.5f;
            if (half < std::numbers::pi_v<float> * 0.5f - 1e-3f)
            {
                const float grow = rect->radius_ * std::tan(std::max(half, 0.f));
                const v3f far = pos + fwd * rect->radius_;
                const v3f farU = halfU + x * grow;
                const v3f farV = halfV + y * grow;
                rectangle(dbg, far, farU, farV, dimmed(color));
                for (const float su : {-1.f, 1.f})
                    for (const float sv : {-1.f, 1.f})
                        dbg.line(pos + halfU * su + halfV * sv, far + farU * su + farV * sv,
                                 dimmed(color));
            }
            else
            {
                dbg.line(pos, pos + fwd * rect->radius_, dimmed(color));
            }
        }

        if (reg.all_of<DirectionalLight_C>(ent.entity_))
        {
            const float length = sunArrowLength(world, pos);
            dbg.arrow(pos, pos + fwd * length, color);
            for (const float su : {-1.f, 1.f})
                for (const float sv : {-1.f, 1.f})
                {
                    const v3f start = pos + (x * su + y * sv) * (length * 0.1f);
                    dbg.line(start, start + fwd * (length * 0.6f), dimmed(color));
                }
        }
    }
}
}  // namespace batap
