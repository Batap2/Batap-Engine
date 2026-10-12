#pragma once

#include "EigenTypes.h"
#include "Terrain/TerrainChunk.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace batap
{
struct TerrainRelief;

using ChunkKeySet = std::unordered_set<ChunkKey, ChunkKeyHash>;

struct LodParams
{
    TerrainGrid grid_;
    int32_t maxLevel_ = 0;
    // Split while the camera is closer than ratio_ x the chunk's side.
    double ratio_ = 2.0;
};

int32_t levelForCell(const TerrainGrid& grid, double cell);

struct Box3d
{
    v3d min_;
    v3d max_;
};

// The ground sampled on a 5 x 5 grid over the node, plus a tenth of the side
// all round. The whole terrain's height bounds would be hundreds of metres
// thick and put the camera inside every node it stands over.
Box3d nodeBounds(const ChunkKey& key, const TerrainRelief& relief, const TerrainGrid& grid);
double distanceTo(const Box3d& box, const v3d& p);

// nodeBounds() behind a cache, since the boxes do not change with the camera.
struct NodeBoundsCache
{
    Box3d get(const ChunkKey& key, const TerrainRelief& relief, const TerrainGrid& grid);
    void clear() { boxes_.clear(); }

   private:
    std::unordered_map<ChunkKey, Box3d, ChunkKeyHash> boxes_;
};

using NodeBoundsFn = std::function<Box3d(const ChunkKey&)>;

// CDLOD: the leaves to show from `camera` (the terrain's frame, metres). A
// node that is a chunk today splits under ratio_ x its side; one that is not
// (split already, or never built) stays split until 1.2 x that, so a still
// camera never flips a node.
std::vector<ChunkKey> selectChunks(const LodParams& params, const v3d& camera,
                                   const ChunkKeySet& existing, const NodeBoundsFn& bounds);
std::vector<ChunkKey> rootChunks(const TerrainGrid& grid);

bool isAncestor(const ChunkKey& ancestor, const ChunkKey& key);

// Face and (u, v) in [-1, 1] under a direction: the inverse of cubeDirection.
struct FaceCoords
{
    int32_t face_ = 0;
    double u_ = 0.0;
    double v_ = 0.0;
};
FaceCoords cubeFaceCoords(const v3d& d);

// The finest chunk of `shown` over a point, among levels lo..hi, or level -1.
ChunkKey shownAt(const FaceCoords& at, const ChunkKeySet& shown, int32_t lo, int32_t hi);

// A chunk of level L slides to its parent's shape between these distances:
// all the way once its parent would be a leaf (ratio x the parent's side),
// starting kMorphBand earlier. Level 0 has no parent: both are zero.
inline constexpr double kMorphBand = 0.3;
double morphEnd(const TerrainGrid& grid, int32_t level, double ratio);
double morphStart(const TerrainGrid& grid, int32_t level, double ratio);

// TerrainChunkGPUData::force_ for a chunk among the chunks shown: a side or
// corner touching a coarser chunk is pinned to the parent shape, which is
// what the coarser chunk draws there, its own slide to its parent included.
// The coarser side needs nothing, nor the edge of the plane.
uint32_t morphForce(const TerrainGrid& grid, const ChunkKey& key, const ChunkKeySet& shown);

// Shaders/Terrain.hlsli on the CPU: where the vertex shader puts vertex
// (i, j) of a chunk. All in the terrain's frame.
struct MorphPoint
{
    v3d position_;
    v3d target_;
    v3d target2_;
};
using MorphFetch = std::function<MorphPoint(int32_t i, int32_t j)>;
v3d morphVertex(int32_t cells, int32_t i, int32_t j, uint32_t force, double start, double end,
                const MorphFetch& at, const v3d& camera);

}  // namespace batap
