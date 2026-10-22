#include "Renderer/DebugDraw.h"

#include <algorithm>
#include <cmath>

namespace batap
{
namespace
{

v3f perpendicular(const v3f& n)
{
    const v3f axis = std::abs(n.y()) > 0.99f ? v3f::UnitX() : v3f::UnitY();
    return n.cross(axis).normalized();
}

}  // namespace

void DebugDraw::pushShape(Shape shape, const m4f& world, const col3& color, float seconds)
{
    shapes_[static_cast<size_t>(shape)].push_back({world, color, time_ + seconds});
}

void DebugDraw::line(const v3f& a, const v3f& b, const col3& color, float seconds)
{
    // The unit line lies along +X with y = z = 0, so the two middle columns
    // are never read: mapping its two vertices needs no orthonormal frame and
    // has no degenerate direction. The rank-1 linear part is deliberate.
    m4f world = m4f::Zero();
    world.col(0).head<3>() = b - a;
    world.col(3).head<3>() = a;
    world(3, 3) = 1.f;
    pushShape(Shape::Line, world, color, seconds);
}

void DebugDraw::arrow(const v3f& a, const v3f& b, const col3& color, float seconds)
{
    line(a, b, color, seconds);

    const v3f delta = b - a;
    const float length = delta.norm();
    if (length < 1e-6f)
        return;

    const v3f dir = delta / length;
    const v3f side = perpendicular(dir);
    const v3f other = dir.cross(side);
    const float head = length * 0.15f;
    const v3f base = b - dir * head;

    line(b, base + side * head * 0.5f, color, seconds);
    line(b, base - side * head * 0.5f, color, seconds);
    line(b, base + other * head * 0.5f, color, seconds);
    line(b, base - other * head * 0.5f, color, seconds);
}

void DebugDraw::axes(const transform& xform, float size, float seconds)
{
    const v3f origin = xform.translation();
    const m3f basis = xform.linear();
    line(origin, origin + basis.col(0).normalized() * size, colors::red, seconds);
    line(origin, origin + basis.col(1).normalized() * size, colors::green, seconds);
    line(origin, origin + basis.col(2).normalized() * size, colors::blue, seconds);
}

void DebugDraw::box(const transform& xform, const v3f& halfExtents, const col3& color,
                    float seconds)
{
    transform scaled = xform;
    scaled.linear() = xform.linear() * halfExtents.asDiagonal();
    pushShape(Shape::Box, scaled.matrix(), color, seconds);
}

void DebugDraw::aabb(const v3f& min, const v3f& max, const col3& color, float seconds)
{
    transform centered = transform::Identity();
    centered.translation() = (min + max) * 0.5f;
    box(centered, (max - min) * 0.5f, color, seconds);
}

void DebugDraw::sphere(const transform& xform, float radius, const col3& color, float seconds)
{
    pushShape(Shape::Sphere, (xform * Eigen::Scaling(radius)).matrix(), color, seconds);
}

void DebugDraw::sphere(const v3f& center, float radius, const col3& color, float seconds)
{
    transform centered = transform::Identity();
    centered.translation() = center;
    sphere(centered, radius, color, seconds);
}

void DebugDraw::capsule(const transform& xform, float halfHeight, float radius, const col3& color,
                        float seconds)
{
    const transform top = xform * Eigen::Translation3f(0.f, halfHeight, 0.f) *
                          Eigen::Scaling(radius, radius, radius);
    const transform bottom = xform * Eigen::Translation3f(0.f, -halfHeight, 0.f) *
                             Eigen::Scaling(radius, -radius, radius);
    const transform side = xform * Eigen::Scaling(radius, halfHeight, radius);

    pushShape(Shape::Hemisphere, top.matrix(), color, seconds);
    pushShape(Shape::Hemisphere, bottom.matrix(), color, seconds);
    pushShape(Shape::CylinderSide, side.matrix(), color, seconds);
}

void DebugDraw::frustum(const m4f& invViewProj, const col3& color, float seconds)
{
    // The unit cube spans [-1, 1] on every axis; Vulkan clip space wants z in
    // [0, 1], hence the half-depth remap before the inverse clip transform.
    m4f ndcFromUnitCube = m4f::Identity();
    ndcFromUnitCube(2, 2) = 0.5f;
    ndcFromUnitCube(2, 3) = 0.5f;
    pushShape(Shape::Box, invViewProj * ndcFromUnitCube, color, seconds);
}

void DebugDraw::endFrame(float dt)
{
    time_ += dt;

    for (auto& shape : shapes_)
        std::erase_if(shape, [this](const ShapeRecord& r) { return r.expiry_ <= time_; });
}

// Through the last row, so the projective frustum moves like the others.
void DebugDraw::shift(const v3f& by)
{
    for (auto& shape : shapes_)
        for (ShapeRecord& r : shape)
            r.world_.topRows<3>() += by * r.world_.row(3);
}

}  // namespace batap
