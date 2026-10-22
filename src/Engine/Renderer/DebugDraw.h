#pragma once

#include "EigenTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace batap
{

namespace colors
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
inline const col3 white{1.f, 1.f, 1.f};
inline const col3 red{1.f, 0.25f, 0.25f};
inline const col3 green{0.35f, 1.f, 0.35f};
inline const col3 blue{0.35f, 0.5f, 1.f};
inline const col3 cyan{0.3f, 0.9f, 1.f};
inline const col3 yellow{1.f, 0.9f, 0.2f};
inline const col3 magenta{1.f, 0.3f, 0.9f};
inline const col3 grey{0.5f, 0.5f, 0.55f};
#pragma clang diagnostic pop
}  // namespace colors

struct DebugDraw
{
    enum class Shape : uint32_t
    {
        Line = 0,
        Box,
        Sphere,
        Hemisphere,
        CylinderSide,
        Count
    };
    static constexpr size_t ShapeCount = static_cast<size_t>(Shape::Count);

    struct ShapeRecord
    {
        m4f world_;
        col3 color_;
        float expiry_ = 0.f;
    };

    void line(const v3f& a, const v3f& b, const col3& color, float seconds = 0.f);
    void arrow(const v3f& a, const v3f& b, const col3& color, float seconds = 0.f);
    void axes(const transform& xform, float size = 1.f, float seconds = 0.f);

    void box(const transform& xform, const v3f& halfExtents, const col3& color,
             float seconds = 0.f);
    void aabb(const v3f& min, const v3f& max, const col3& color, float seconds = 0.f);
    void sphere(const transform& xform, float radius, const col3& color, float seconds = 0.f);
    void sphere(const v3f& center, float radius, const col3& color, float seconds = 0.f);
    void capsule(const transform& xform, float halfHeight, float radius, const col3& color,
                 float seconds = 0.f);

    // The matrix is projective, unlike every other shape: the unit cube is
    // dragged back through the inverse clip transform, so the vertex shader
    // has to divide by w.
    void frustum(const m4f& invViewProj, const col3& color, float seconds = 0.f);

    // Drops what this frame drew and what has run out of time. Called once the
    // frame has been handed to the renderer.
    void endFrame(float dt);
    void shift(const v3f& by);

    const std::vector<ShapeRecord>& shapes(Shape shape) const
    {
        return shapes_[static_cast<size_t>(shape)];
    }

   private:
    void pushShape(Shape shape, const m4f& world, const col3& color, float seconds);

    float time_ = 0.f;
    std::array<std::vector<ShapeRecord>, ShapeCount> shapes_;
};

}  // namespace batap
