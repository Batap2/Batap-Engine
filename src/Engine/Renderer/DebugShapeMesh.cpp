#include "Renderer/DebugShapeMesh.h"

#include <cmath>

namespace batap
{
namespace
{

constexpr float kPi = 3.14159265358979f;
constexpr uint32_t kRingSegments = 32;

// A box is the [-1, 1] cube, so its matrix carries the half extents.
constexpr std::array<std::array<float, 3>, 8> kBoxCorners = {{{-1, -1, -1},
                                                              {1, -1, -1},
                                                              {1, 1, -1},
                                                              {-1, 1, -1},
                                                              {-1, -1, 1},
                                                              {1, -1, 1},
                                                              {1, 1, 1},
                                                              {-1, 1, 1}}};
constexpr std::array<uint32_t, 24> kBoxEdges = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6,
                                                6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};

void writeVertex(DebugVertexGPUData& out, float x, float y, float z)
{
    out.pos_[0] = x;
    out.pos_[1] = y;
    out.pos_[2] = z;
    out.pad_ = 0.f;
}

void writeVertex(DebugVertexGPUData& out, const std::array<float, 3>& p)
{
    writeVertex(out, p[0], p[1], p[2]);
}

// axis 0/1/2 says which coordinate stays at zero, so the three calls give the
// YZ, XZ and XY great circles.
void appendArc(std::vector<DebugVertexGPUData>& verts, uint32_t axis, float fromRadians,
               float toRadians, uint32_t segments)
{
    auto at = [&](float angle)
    {
        const float c = std::cos(angle);
        const float sn = std::sin(angle);
        switch (axis)
        {
            case 0:
                return std::array<float, 3>{0.f, c, sn};
            case 1:
                return std::array<float, 3>{c, 0.f, sn};
            default:
                return std::array<float, 3>{c, sn, 0.f};
        }
    };

    const float step = (toRadians - fromRadians) / static_cast<float>(segments);
    for (uint32_t i = 0; i < segments; ++i)
    {
        writeVertex(verts.emplace_back(), at(fromRadians + step * static_cast<float>(i)));
        writeVertex(verts.emplace_back(), at(fromRadians + step * static_cast<float>(i + 1)));
    }
}

}  // namespace

std::vector<DebugVertexGPUData> buildDebugWireframes(
    std::array<DebugShapeSlice, DebugDraw::ShapeCount>& slices)
{
    std::vector<DebugVertexGPUData> verts;

    auto begin = [&](DebugDraw::Shape shape) -> DebugShapeSlice&
    {
        DebugShapeSlice& slice = slices[static_cast<size_t>(shape)];
        slice.firstVertex_ = static_cast<uint32_t>(verts.size());
        return slice;
    };
    auto end = [&](DebugShapeSlice& slice)
    { slice.vertexCount_ = static_cast<uint32_t>(verts.size()) - slice.firstVertex_; };

    DebugShapeSlice& line = begin(DebugDraw::Shape::Line);
    writeVertex(verts.emplace_back(), 0.f, 0.f, 0.f);
    writeVertex(verts.emplace_back(), 1.f, 0.f, 0.f);
    end(line);

    DebugShapeSlice& box = begin(DebugDraw::Shape::Box);
    for (uint32_t index : kBoxEdges)
        writeVertex(verts.emplace_back(), kBoxCorners[index]);
    end(box);

    DebugShapeSlice& sphere = begin(DebugDraw::Shape::Sphere);
    for (uint32_t axis = 0; axis < 3; ++axis)
        appendArc(verts, axis, 0.f, 2.f * kPi, kRingSegments);
    end(sphere);

    // Dome pointing +Y: the equator, then two meridians.
    DebugShapeSlice& hemisphere = begin(DebugDraw::Shape::Hemisphere);
    appendArc(verts, 1, 0.f, 2.f * kPi, kRingSegments);
    appendArc(verts, 0, 0.f, kPi, kRingSegments / 2);
    appendArc(verts, 2, 0.f, kPi, kRingSegments / 2);
    end(hemisphere);

    DebugShapeSlice& cylinder = begin(DebugDraw::Shape::CylinderSide);
    for (const auto& xz : {std::array<float, 2>{1.f, 0.f}, std::array<float, 2>{-1.f, 0.f},
                           std::array<float, 2>{0.f, 1.f}, std::array<float, 2>{0.f, -1.f}})
    {
        writeVertex(verts.emplace_back(), xz[0], -1.f, xz[1]);
        writeVertex(verts.emplace_back(), xz[0], 1.f, xz[1]);
    }
    end(cylinder);

    return verts;
}
}  // namespace batap
