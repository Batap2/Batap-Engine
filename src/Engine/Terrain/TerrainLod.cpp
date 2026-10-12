#include "Terrain/TerrainLod.h"

#include "Terrain/TerrainRelief.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace batap
{
namespace
{

constexpr double kMergeHysteresis = 1.2;

ChunkKey parentOf(const ChunkKey& key)
{
    return {key.face_, key.level_ - 1, key.x_ >> 1, key.y_ >> 1};
}

std::array<ChunkKey, 4> childrenOf(const ChunkKey& key)
{
    const int32_t l = key.level_ + 1;
    const int32_t x = key.x_ * 2;
    const int32_t y = key.y_ * 2;
    return {{{key.face_, l, x, y},
             {key.face_, l, x + 1, y},
             {key.face_, l, x, y + 1},
             {key.face_, l, x + 1, y + 1}}};
}

void select(const ChunkKey& key, const LodParams& params, const v3d& camera,
            const ChunkKeySet& existing, const NodeBoundsFn& bounds, std::vector<ChunkKey>& out)
{
    const double side = nodeSide(params.grid_, key.level_);
    const double d = distanceTo(bounds(key), camera);
    const double splitAt = params.ratio_ * side * (existing.contains(key) ? 1.0 : kMergeHysteresis);
    if (key.level_ >= params.maxLevel_ || d >= splitAt)
    {
        out.push_back(key);
        return;
    }
    for (int32_t cy = 0; cy < 2; ++cy)
        for (int32_t cx = 0; cx < 2; ++cx)
            select({key.face_, key.level_ + 1, key.x_ * 2 + cx, key.y_ * 2 + cy}, params, camera,
                   existing, bounds, out);
}

}  // namespace

int32_t levelForCell(const TerrainGrid& grid, double cell)
{
    if (cell <= 0.0)
        return kMaxTerrainLevel;
    const double rootCell = cellSize(grid, 0);
    const int32_t level = static_cast<int32_t>(std::ceil(std::log2(std::max(rootCell / cell, 1.0))));
    return std::clamp(level, 0, kMaxTerrainLevel);
}

Box3d nodeBounds(const ChunkKey& key, const TerrainRelief& relief, const TerrainGrid& grid)
{
    const double span = std::ldexp(2.0, -key.level_);
    const double u0 = -1.0 + span * key.x_;
    const double v0 = -1.0 + span * key.y_;
    Box3d box{v3d::Constant(1e300), v3d::Constant(-1e300)};
    for (int32_t j = 0; j <= 4; ++j)
        for (int32_t i = 0; i <= 4; ++i)
        {
            const SurfacePoint sp =
                surfacePoint(grid, key.face_, u0 + span * i / 4.0, v0 + span * j / 4.0);
            const v3d p =
                sp.base_ +
                sp.up_ * relief.sample({sp.base_, sp.up_, key.level_, cellSize(grid, key.level_)})
                             .height_;
            box.min_ = box.min_.cwiseMin(p);
            box.max_ = box.max_.cwiseMax(p);
        }
    const v3d margin = v3d::Constant(0.1 * nodeSide(grid, key.level_));
    box.min_ -= margin;
    box.max_ += margin;
    return box;
}

double distanceTo(const Box3d& box, const v3d& p)
{
    const v3d q = p.cwiseMax(box.min_).cwiseMin(box.max_);
    return (p - q).norm();
}

Box3d NodeBoundsCache::get(const ChunkKey& key, const TerrainRelief& relief,
                           const TerrainGrid& grid)
{
    if (const auto it = boxes_.find(key); it != boxes_.end())
        return it->second;
    const Box3d box = nodeBounds(key, relief, grid);
    boxes_.emplace(key, box);
    return box;
}

std::vector<ChunkKey> selectChunks(const LodParams& params, const v3d& camera,
                                   const ChunkKeySet& existing, const NodeBoundsFn& bounds)
{
    std::vector<ChunkKey> out;
    for (const ChunkKey& root : rootChunks(params.grid_))
        select(root, params, camera, existing, bounds, out);
    return out;
}

std::vector<ChunkKey> rootChunks(const TerrainGrid& grid)
{
    std::vector<ChunkKey> roots;
    for (int32_t face = 0; face < rootCount(grid); ++face)
        roots.push_back({face, 0, 0, 0});
    return roots;
}

ChunkKeySet quadtreeOf(const std::vector<ChunkKey>& leaves)
{
    ChunkKeySet tree;
    tree.reserve(leaves.size() * 4 / 3 + 8);
    for (ChunkKey key : leaves)
        while (tree.insert(key).second && key.level_ > 0)
            key = parentOf(key);
    return tree;
}

std::optional<ChunkKey> neighbour(const TerrainGrid& grid, const ChunkKey& key, int32_t dx,
                                  int32_t dy)
{
    const int32_t n = 1 << key.level_;
    const int32_t x = key.x_ + dx;
    const int32_t y = key.y_ + dy;
    if (x >= 0 && x < n && y >= 0 && y < n)
        return ChunkKey{key.face_, key.level_, x, y};
    if (grid.shape_ == TerrainShape::Plane)
        return std::nullopt;
    // Just past the side or corner, onto the face that holds it.
    const double span = std::ldexp(2.0, -key.level_);
    const double reach = 0.5 * span + 1e-3 * span;
    const FaceCoords fc =
        cubeFaceCoords(cubeDirection(key.face_, -1.0 + span * (key.x_ + 0.5) + dx * reach,
                                     -1.0 + span * (key.y_ + 0.5) + dy * reach));
    return ChunkKey{fc.face_, key.level_,
                    std::clamp(static_cast<int32_t>(std::floor((fc.u_ + 1.0) / span)), 0, n - 1),
                    std::clamp(static_cast<int32_t>(std::floor((fc.v_ + 1.0) / span)), 0, n - 1)};
}

std::vector<ChunkKey> shownChunks(const TerrainGrid& grid, const ChunkKeySet& quadtree,
                                  const ChunkTest& built)
{
    std::vector<ChunkKey> shown;
    std::vector<ChunkKey> level = rootChunks(grid);
    ChunkKeySet reached(level.begin(), level.end());
    const auto besideReached = [&](const ChunkKey& key)
    {
        for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dx = -1; dx <= 1; ++dx)
                if (dx != 0 || dy != 0)
                    if (const std::optional<ChunkKey> n = neighbour(grid, key, dx, dy);
                        n && !reached.contains(*n))
                        return false;
        return true;
    };
    while (!level.empty())
    {
        std::vector<ChunkKey> next;
        for (const ChunkKey& key : level)
        {
            const std::array<ChunkKey, 4> children = childrenOf(key);
            if (quadtree.contains(children[0]) &&
                std::all_of(children.begin(), children.end(), built) && besideReached(key))
                next.insert(next.end(), children.begin(), children.end());
            else if (built(key))
                shown.push_back(key);
        }
        reached.insert(next.begin(), next.end());
        level = std::move(next);
    }
    return shown;
}

std::vector<ChunkKey> buildFrontier(const ChunkKeySet& quadtree, const ChunkTest& built)
{
    std::vector<ChunkKey> frontier;
    for (const ChunkKey& key : quadtree)
        if (!built(key) && (key.level_ == 0 || built(parentOf(key))))
            frontier.push_back(key);
    return frontier;
}

bool isAncestor(const ChunkKey& ancestor, const ChunkKey& key)
{
    if (ancestor.face_ != key.face_ || ancestor.level_ >= key.level_)
        return false;
    const int32_t shift = key.level_ - ancestor.level_;
    return (key.x_ >> shift) == ancestor.x_ && (key.y_ >> shift) == ancestor.y_;
}

FaceCoords cubeFaceCoords(const v3d& d)
{
    const std::array<double, 3> a{std::fabs(d.x()), std::fabs(d.y()), std::fabs(d.z())};
    FaceCoords out;
    double t = 0.0;
    double b = 0.0;
    double n = 1.0;
    if (a[0] >= a[1] && a[0] >= a[2])
    {
        // +X: tangent -Z, bitangent +Y; -X: tangent +Z, bitangent +Y.
        out.face_ = d.x() >= 0.0 ? 0 : 1;
        n = a[0];
        t = d.x() >= 0.0 ? -d.z() : d.z();
        b = d.y();
    }
    else if (a[1] >= a[2])
    {
        // +Y: tangent +X, bitangent -Z; -Y: tangent +X, bitangent +Z.
        out.face_ = d.y() >= 0.0 ? 2 : 3;
        n = a[1];
        t = d.x();
        b = d.y() >= 0.0 ? -d.z() : d.z();
    }
    else
    {
        // +Z: tangent +X, bitangent +Y; -Z: tangent -X, bitangent +Y.
        out.face_ = d.z() >= 0.0 ? 4 : 5;
        n = a[2];
        t = d.z() >= 0.0 ? d.x() : -d.x();
        b = d.y();
    }
    out.u_ = std::atan(t / n) / (std::numbers::pi / 4.0);
    out.v_ = std::atan(b / n) / (std::numbers::pi / 4.0);
    return out;
}

ChunkKey shownAt(const FaceCoords& fc, const ChunkKeySet& shown, int32_t lo, int32_t hi)
{
    for (int32_t level = std::min(hi, kMaxTerrainLevel); level >= std::max(lo, 0); --level)
    {
        const int32_t n = 1 << level;
        const double span = std::ldexp(2.0, -level);
        const ChunkKey k{fc.face_, level,
                         std::clamp(static_cast<int32_t>(std::floor((fc.u_ + 1.0) / span)), 0, n - 1),
                         std::clamp(static_cast<int32_t>(std::floor((fc.v_ + 1.0) / span)), 0, n - 1)};
        if (shown.contains(k))
            return k;
    }
    return {0, -1, 0, 0};
}

double morphEnd(const TerrainGrid& grid, int32_t level, double ratio)
{
    return level <= 0 ? 0.0 : ratio * nodeSide(grid, level - 1);
}

double morphStart(const TerrainGrid& grid, int32_t level, double ratio)
{
    return (1.0 - kMorphBand) * morphEnd(grid, level, ratio);
}

uint32_t morphForce(const TerrainGrid& grid, const ChunkKey& key, const ChunkKeySet& shown)
{
    const double span = std::ldexp(2.0, -key.level_);
    const double u0 = -1.0 + span * key.x_;
    const double v0 = -1.0 + span * key.y_;
    const double eps = 1e-3 * span;
    const auto levelAt = [&](double u, double v)
    {
        const int32_t lo = key.level_ - 2;
        const int32_t hi = key.level_ + 2;
        if (grid.shape_ == TerrainShape::Sphere)
            return shownAt(cubeFaceCoords(cubeDirection(key.face_, u, v)), shown, lo, hi).level_;
        if (std::fabs(u) > 1.0 || std::fabs(v) > 1.0)
            return -1;
        return shownAt(FaceCoords{0, u, v}, shown, lo, hi).level_;
    };

    // Two probes per side, a quarter of the side in from each end, just
    // outside it; one diagonal probe per corner.
    const double q1 = 0.25 * span;
    const double q3 = 0.75 * span;
    const std::array<std::array<int32_t, 2>, 4> sides{{
        {levelAt(u0 + q1, v0 - eps), levelAt(u0 + q3, v0 - eps)},
        {levelAt(u0 + span + eps, v0 + q1), levelAt(u0 + span + eps, v0 + q3)},
        {levelAt(u0 + q3, v0 + span + eps), levelAt(u0 + q1, v0 + span + eps)},
        {levelAt(u0 - eps, v0 + q3), levelAt(u0 - eps, v0 + q1)},
    }};
    const std::array<int32_t, 4> diagonals{
        levelAt(u0 - eps, v0 - eps), levelAt(u0 + span + eps, v0 - eps),
        levelAt(u0 + span + eps, v0 + span + eps), levelAt(u0 - eps, v0 + span + eps)};
    // Corner c sits between side (c + 3) % 4, end 1, and side c, end 0.
    const auto coarser = [&](int32_t l) { return l >= 0 && l < key.level_; };

    uint32_t force = 0;
    for (size_t s = 0; s < 4; ++s)
        if (coarser(sides[s][0]) || coarser(sides[s][1]))
            force |= 1u << s;
    for (size_t c = 0; c < 4; ++c)
        for (const int32_t l : {sides[(c + 3) % 4][1], sides[c][0], diagonals[c]})
            if (coarser(l))
                force |= 1u << (4 + c);
    return force;
}

std::vector<ChunkKey> chunksTouching(const TerrainGrid& grid, const std::vector<ChunkKey>& keys,
                                     const ChunkKeySet& shown)
{
    ChunkKeySet found;
    const auto keep = [&](const ChunkKey& key)
    {
        if (shown.contains(key))
            found.insert(key);
    };
    for (const ChunkKey& key : keys)
    {
        keep(key);
        for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dx = -1; dx <= 1; ++dx)
            {
                const std::optional<ChunkKey> n = dx != 0 || dy != 0 ? neighbour(grid, key, dx, dy)
                                                                     : std::nullopt;
                if (!n)
                    continue;
                keep(*n);
                if (n->level_ > 0)
                    keep(parentOf(*n));
                for (const ChunkKey& child : childrenOf(*n))
                    keep(child);
            }
    }
    return {found.begin(), found.end()};
}

v3d morphVertex(int32_t cells, int32_t i, int32_t j, uint32_t force, double start, double end,
                const MorphFetch& at, const v3d& camera)
{
    const auto ramp = [](double d, double a, double b)
    { return std::clamp((d - a) / std::max(b - a, 1e-6), 0.0, 1.0); };
    const int32_t last = cells;
    uint32_t border = 0;
    border |= j == 0 ? 1u : 0u;
    border |= i == last ? 2u : 0u;
    border |= j == last ? 4u : 0u;
    border |= i == 0 ? 8u : 0u;
    border |= (i == 0 && j == 0) ? 16u : 0u;
    border |= (i == last && j == 0) ? 32u : 0u;
    border |= (i == last && j == last) ? 64u : 0u;
    border |= (i == 0 && j == last) ? 128u : 0u;

    const double start2 = 2.0 * start;
    const double end2 = 2.0 * end;
    const MorphPoint p = at(i, j);
    const double k2 = ramp((p.target2_ - camera).norm(), start2, end2);
    const v3d inner = p.target_ + k2 * (p.target2_ - p.target_);
    const uint32_t pinned = border & force;
    if (pinned == 0)
    {
        const double k = ramp((p.target_ - camera).norm(), start, end);
        return p.position_ + k * (inner - p.position_);
    }
    const bool oddI = (pinned & 5u) != 0 && (i & 1) != 0;
    const bool oddJ = (pinned & 10u) != 0 && (j & 1) != 0;
    if (!oddI && !oddJ)
        return inner;
    // The coarser neighbour draws a straight segment between the two even
    // vertices around, each sliding on its own.
    const MorphPoint a = oddI ? at(i - 1, j) : at(i, j - 1);
    const MorphPoint b = oddI ? at(i + 1, j) : at(i, j + 1);
    const double kA = ramp((a.target2_ - camera).norm(), start2, end2);
    const double kB = ramp((b.target2_ - camera).norm(), start2, end2);
    return 0.5 * ((a.target_ + kA * (a.target2_ - a.target_)) +
                  (b.target_ + kB * (b.target2_ - b.target_)));
}

}  // namespace batap
