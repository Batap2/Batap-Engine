#include "Terrain/TerrainChunk.h"

#include "Flatten.h"
#include "Terrain/TerrainRelief.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numbers>
#include <tuple>
#include <utility>

namespace batap
{
namespace
{

struct FaceFrame
{
    v3d normal_;
    v3d tangent_;
    v3d bitangent_;
};

const std::array<FaceFrame, 6>& faceFrames()
{
    static const std::array<FaceFrame, 6> frames{{
        {v3d::UnitX(), -v3d::UnitZ(), v3d::UnitY()},
        {-v3d::UnitX(), v3d::UnitZ(), v3d::UnitY()},
        {v3d::UnitY(), v3d::UnitX(), -v3d::UnitZ()},
        {-v3d::UnitY(), v3d::UnitX(), v3d::UnitZ()},
        {v3d::UnitZ(), v3d::UnitX(), v3d::UnitY()},
        {-v3d::UnitZ(), -v3d::UnitX(), v3d::UnitY()},
    }};
    return frames;
}

}  // namespace

int32_t clampChunkCells(int32_t cells)
{
    return static_cast<int32_t>(
        std::bit_floor(static_cast<uint32_t>(std::clamp(cells, kMinChunkCells, kMaxChunkCells))));
}

v3d cubeDirection(int32_t face, double u, double v)
{
    const FaceFrame& f = faceFrames()[static_cast<size_t>(std::clamp(face, 0, 5))];
    const double a = std::tan(u * std::numbers::pi / 4.0);
    const double b = std::tan(v * std::numbers::pi / 4.0);
    return (f.normal_ + f.tangent_ * a + f.bitangent_ * b).normalized();
}

double nodeSide(const TerrainGrid& grid, int32_t level)
{
    const double root = grid.shape_ == TerrainShape::Plane ? grid.size_
                                                           : std::numbers::pi / 2.0 * grid.size_;
    return root * std::ldexp(1.0, -level);
}

double cellSize(const TerrainGrid& grid, int32_t level)
{
    return nodeSide(grid, level) / grid.cells_;
}

int32_t rootCount(const TerrainGrid& grid)
{
    return grid.shape_ == TerrainShape::Plane ? 1 : 6;
}

SurfacePoint surfacePoint(const TerrainGrid& grid, int32_t face, double u, double v)
{
    if (grid.shape_ == TerrainShape::Plane)
        return {v3d{0.5 * grid.size_ * u, 0.0, -0.5 * grid.size_ * v}, v3d::UnitY()};
    const v3d d = cubeDirection(face, u, v);
    return {d * grid.size_, d};
}

v3d upAt(const TerrainGrid& grid, const v3d& p)
{
    return grid.shape_ == TerrainShape::Plane ? v3d::UnitY() : v3d{p.normalized()};
}

ChunkMesh buildChunk(const TerrainRelief& relief, const TerrainGrid& grid, const ChunkKey& key)
{
    const int32_t cells = grid.cells_;
    const int32_t side = cells + 1;
    const int32_t paddedSide = side + 2;
    const auto padded = [&](int32_t i, int32_t j)
    { return static_cast<size_t>((j + 1) * paddedSide + (i + 1)); };
    const auto at = [&](int32_t i, int32_t j) { return static_cast<size_t>(j * side + i); };

    const int32_t level = std::clamp(key.level_, 0, kMaxTerrainLevel);
    const double span = std::ldexp(2.0, -level);
    const double u0 = -1.0 + span * key.x_;
    const double v0 = -1.0 + span * key.y_;
    const auto sample = [&](double i, double j, int32_t l)
    {
        const SurfacePoint p =
            surfacePoint(grid, key.face_, u0 + span * i / cells, v0 + span * j / cells);
        const TerrainRelief::Sample s = relief.sample({p.base_, p.up_, l, cellSize(grid, l)});
        return std::pair<v3d, int32_t>{p.base_ + p.up_ * s.height_, s.zone_};
    };

    ChunkMesh out;
    out.cells_ = cells;
    out.center_ = sample(0.5 * cells, 0.5 * cells, level).first;

    // One ring more than the chunk, for the normals of its border.
    std::vector<v3d> world(static_cast<size_t>(paddedSide * paddedSide));
    std::vector<int32_t> zones(static_cast<size_t>(paddedSide * paddedSide));
    for (int32_t j = -1; j <= side; ++j)
        for (int32_t i = -1; i <= side; ++i)
            std::tie(world[padded(i, j)], zones[padded(i, j)]) = sample(i, j, level);

    const size_t count = static_cast<size_t>(side * side);
    out.positions_.resize(count);
    out.normals_.resize(count);
    for (int32_t j = 0; j < side; ++j)
        for (int32_t i = 0; i < side; ++i)
        {
            const v3d du = world[padded(i + 1, j)] - world[padded(i - 1, j)];
            const v3d dv = world[padded(i, j + 1)] - world[padded(i, j - 1)];
            out.positions_[at(i, j)] = (world[padded(i, j)] - out.center_).cast<float>();
            out.normals_[at(i, j)] = du.cross(dv).normalized().cast<float>();
        }

    // The parent's vertices are this chunk's even ones, one level coarser;
    // between them the parent is flat, with its diagonals from (0,0) to
    // (1,1) in every cell, the same as the triangles below. Its normals are
    // the parent chunk's own: central differences one parent cell apart,
    // hence the parent ring at -2 and cells + 2.
    out.parentPositions_.resize(count);
    out.parentNormals_.resize(count);
    if (level == 0)
    {
        out.parentPositions_ = out.positions_;
        out.parentNormals_ = out.normals_;
    }
    else
    {
        const int32_t parentSide = cells / 2 + 3;
        const auto ring = [&](int32_t i, int32_t j)
        { return static_cast<size_t>((j / 2 + 1) * parentSide + (i / 2 + 1)); };
        std::vector<v3d> parentRing(static_cast<size_t>(parentSide * parentSide));
        for (int32_t j = -2; j <= cells + 2; j += 2)
            for (int32_t i = -2; i <= cells + 2; i += 2)
                parentRing[ring(i, j)] = sample(i, j, level - 1).first;

        std::vector<v3d> parent(count);
        std::vector<v3d> parentNormal(count);
        for (int32_t j = 0; j <= cells; j += 2)
            for (int32_t i = 0; i <= cells; i += 2)
            {
                parent[at(i, j)] = parentRing[ring(i, j)];
                const v3d du = parentRing[ring(i + 2, j)] - parentRing[ring(i - 2, j)];
                const v3d dv = parentRing[ring(i, j + 2)] - parentRing[ring(i, j - 2)];
                parentNormal[at(i, j)] = du.cross(dv).normalized();
            }
        const auto between = [&](std::vector<v3d>& v, int32_t i, int32_t j)
        {
            const bool oddI = (i & 1) != 0;
            const bool oddJ = (j & 1) != 0;
            if (oddI && oddJ)
                v[at(i, j)] = 0.5 * (v[at(i - 1, j - 1)] + v[at(i + 1, j + 1)]);
            else if (oddI)
                v[at(i, j)] = 0.5 * (v[at(i - 1, j)] + v[at(i + 1, j)]);
            else if (oddJ)
                v[at(i, j)] = 0.5 * (v[at(i, j - 1)] + v[at(i, j + 1)]);
        };
        for (int32_t j = 0; j <= cells; ++j)
            for (int32_t i = 0; i <= cells; ++i)
            {
                between(parent, i, j);
                between(parentNormal, i, j);
            }
        for (size_t k = 0; k < count; ++k)
        {
            out.parentPositions_[k] = (parent[k] - out.center_).cast<float>();
            out.parentNormals_[k] = parentNormal[k].normalized().cast<float>();
        }
    }

    // The grandparent's shape, for a child to show its parent while that
    // parent is itself sliding to the grandparent: vertices every 4 cells,
    // normals one grandparent cell apart, and in between its triangles, whose
    // diagonals the parent's own triangles nest in.
    out.grandparentNormals_.resize(count);
    if (level <= 1)
    {
        out.grandparentPositions_ = out.parentPositions_;
        out.grandparentNormals_ = out.parentNormals_;
    }
    else
    {
        const int32_t gpCells = cells / 4;
        const int32_t gpSide = gpCells + 3;
        const auto ring = [&](int32_t gi, int32_t gj)
        { return static_cast<size_t>((gj + 1) * gpSide + (gi + 1)); };
        std::vector<v3d> gp(static_cast<size_t>(gpSide * gpSide));
        for (int32_t gj = -1; gj <= gpCells + 1; ++gj)
            for (int32_t gi = -1; gi <= gpCells + 1; ++gi)
                gp[ring(gi, gj)] = sample(4 * gi, 4 * gj, level - 2).first;
        std::vector<v3d> gpNormal(static_cast<size_t>(gpSide * gpSide));
        for (int32_t gj = 0; gj <= gpCells; ++gj)
            for (int32_t gi = 0; gi <= gpCells; ++gi)
            {
                const v3d du = gp[ring(gi + 1, gj)] - gp[ring(gi - 1, gj)];
                const v3d dv = gp[ring(gi, gj + 1)] - gp[ring(gi, gj - 1)];
                gpNormal[ring(gi, gj)] = du.cross(dv).normalized();
            }
        const auto onTriangles = [&](const std::vector<v3d>& v, int32_t i, int32_t j)
        {
            const int32_t gi = std::min(i / 4, gpCells - 1);
            const int32_t gj = std::min(j / 4, gpCells - 1);
            const double s = (i - 4 * gi) / 4.0;
            const double t = (j - 4 * gj) / 4.0;
            const v3d& p00 = v[ring(gi, gj)];
            const v3d& p10 = v[ring(gi + 1, gj)];
            const v3d& p11 = v[ring(gi + 1, gj + 1)];
            const v3d& p01 = v[ring(gi, gj + 1)];
            return s >= t ? v3d{p00 + s * (p10 - p00) + t * (p11 - p10)}
                          : v3d{p00 + t * (p01 - p00) + s * (p11 - p01)};
        };
        out.grandparentPositions_.resize(count);
        for (int32_t j = 0; j <= cells; ++j)
            for (int32_t i = 0; i <= cells; ++i)
            {
                out.grandparentPositions_[at(i, j)] =
                    (onTriangles(gp, i, j) - out.center_).cast<float>();
                out.grandparentNormals_[at(i, j)] =
                    onTriangles(gpNormal, i, j).normalized().cast<float>();
            }
    }

    const size_t triangles = static_cast<size_t>(cells * cells * 2);
    std::vector<std::array<uint16_t, 3>> tris;
    tris.reserve(triangles);
    out.strata_.reserve(triangles);
    const auto classify = [&](size_t a, size_t b, size_t c, int32_t ia, int32_t ja, int32_t ib,
                              int32_t jb, int32_t ic, int32_t jc)
    {
        const v3d& pa = world[padded(ia, ja)];
        const v3d& pb = world[padded(ib, jb)];
        const v3d& pc = world[padded(ic, jc)];
        const v3d n = (pb - pa).cross(pc - pa).normalized();
        const v3d up = upAt(grid, pa + pb + pc);
        const uint8_t s = relief.stratum(
            n.dot(up), {zones[padded(ia, ja)], zones[padded(ib, jb)], zones[padded(ic, jc)]});
        tris.push_back(
            {static_cast<uint16_t>(a), static_cast<uint16_t>(b), static_cast<uint16_t>(c)});
        out.strata_.push_back(std::min(s, static_cast<uint8_t>(kMaxTerrainStrata - 1)));
        ++out.ranges_[out.strata_.back()].count_;
    };
    for (int32_t j = 0; j < cells; ++j)
        for (int32_t i = 0; i < cells; ++i)
        {
            const size_t a = at(i, j);
            const size_t b = at(i + 1, j);
            const size_t c = at(i + 1, j + 1);
            const size_t d = at(i, j + 1);
            classify(a, b, c, i, j, i + 1, j, i + 1, j + 1);
            classify(a, c, d, i, j, i + 1, j + 1, i, j + 1);
        }

    std::array<uint32_t, kMaxTerrainStrata> cursor{};
    uint32_t offset = 0;
    for (size_t s = 0; s < kMaxTerrainStrata; ++s)
    {
        cursor[s] = offset;
        out.ranges_[s].count_ *= 3;
        out.ranges_[s].offset_ = offset * 3;
        offset += out.ranges_[s].count_ / 3;
    }
    out.indices_.resize(triangles * 3);
    for (size_t t = 0; t < triangles; ++t)
    {
        const size_t slot = static_cast<size_t>(cursor[out.strata_[t]]++) * 3;
        out.indices_[slot] = tris[t][0];
        out.indices_[slot + 1] = tris[t][1];
        out.indices_[slot + 2] = tris[t][2];
    }
    return out;
}

uint32_t packNormal(const v3f& n)
{
    const float l1 = std::abs(n.x()) + std::abs(n.y()) + std::abs(n.z());
    v2f e{n.x() / l1, n.y() / l1};
    if (n.z() < 0.f)
        e = v2f{(1.f - std::abs(e.y())) * (e.x() >= 0.f ? 1.f : -1.f),
                (1.f - std::abs(e.x())) * (e.y() >= 0.f ? 1.f : -1.f)};
    const auto snorm = [](float v)
    {
        const auto q = static_cast<int16_t>(std::lround(std::clamp(v, -1.f, 1.f) * 32767.f));
        return static_cast<uint32_t>(static_cast<uint16_t>(q));
    };
    return snorm(e.x()) | (snorm(e.y()) << 16);
}

v3f unpackNormal(uint32_t packed)
{
    const auto snorm = [](uint32_t bits)
    {
        const auto q = static_cast<int16_t>(static_cast<uint16_t>(bits & 0xFFFFu));
        return std::max(static_cast<float>(q) / 32767.f, -1.f);
    };
    v3f n{snorm(packed), snorm(packed >> 16), 0.f};
    n.z() = 1.f - std::abs(n.x()) - std::abs(n.y());
    const float t = std::max(-n.z(), 0.f);
    n.x() += n.x() >= 0.f ? -t : t;
    n.y() += n.y() >= 0.f ? -t : t;
    return n.normalized();
}

std::vector<TerrainVertexGPUData> packVertices(const ChunkMesh& mesh)
{
    std::vector<TerrainVertexGPUData> out(mesh.positions_.size());
    for (size_t k = 0; k < out.size(); ++k)
    {
        flatten(out[k].position_, mesh.positions_[k]);
        out[k].normal_ = packNormal(mesh.normals_[k]);
        flatten(out[k].target_, mesh.parentPositions_[k]);
        out[k].targetNormal_ = packNormal(mesh.parentNormals_[k]);
        flatten(out[k].target2_, mesh.grandparentPositions_[k]);
        out[k].target2Normal_ = packNormal(mesh.grandparentNormals_[k]);
    }
    return out;
}

}  // namespace batap
