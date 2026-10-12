#pragma once

#include "EigenTypes.h"
#include "Shaders/ShaderInterop.h"
#include "Terrain/TerrainShape.h"

#include <array>
#include <cstdint>
#include <vector>

namespace batap
{

struct TerrainRelief;

// Cells along a chunk's side: a power of two in this range.
inline constexpr int32_t kChunkCells = 32;
inline constexpr int32_t kMinChunkCells = 16;
inline constexpr int32_t kMaxChunkCells = 128;
int32_t clampChunkCells(int32_t cells);

inline constexpr int32_t kMaxTerrainLevel = 24;
inline constexpr size_t kMaxTerrainStrata = 8;

struct TerrainGrid
{
    TerrainShape shape_ = TerrainShape::Plane;
    // The sphere's radius or the plane's side, in metres.
    double size_ = 1.0;
    int32_t cells_ = kChunkCells;
};

// A node's side: an arc of the sphere, or a stretch of the plane.
double nodeSide(const TerrainGrid& grid, int32_t level);
double cellSize(const TerrainGrid& grid, int32_t level);
int32_t rootCount(const TerrainGrid& grid);

struct ChunkKey
{
    int32_t face_ = 0;
    int32_t level_ = 0;
    int32_t x_ = 0;
    int32_t y_ = 0;

    // Spelled out: a defaulted one makes MSVC's std::find take its vectorised
    // path, which has no case for a 16-byte key.
    bool operator==(const ChunkKey& o) const
    {
        return face_ == o.face_ && level_ == o.level_ && x_ == o.x_ && y_ == o.y_;
    }
};

struct ChunkKeyHash
{
    size_t operator()(const ChunkKey& k) const noexcept
    {
        uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(k.face_));
        h = h * 0x9E3779B97F4A7C15ull + static_cast<uint32_t>(k.level_);
        h = h * 0x9E3779B97F4A7C15ull + static_cast<uint32_t>(k.x_);
        h = h * 0x9E3779B97F4A7C15ull + static_cast<uint32_t>(k.y_);
        return static_cast<size_t>(h ^ (h >> 29));
    }
};

// Face frames: tangent x bitangent = outward normal, so the grid winds
// counter-clockwise seen from outside and dP/du x dP/dv points out.
v3d cubeDirection(int32_t face, double u, double v);

struct SurfacePoint
{
    v3d base_;
    v3d up_;
};

// (u, v) in [-1, 1] on a root. The plane's +u is +X and +v is -Z: it winds
// like a cube face.
SurfacePoint surfacePoint(const TerrainGrid& grid, int32_t face, double u, double v);

// The vertical over a point of the terrain, or over a sum of them.
v3d upAt(const TerrainGrid& grid, const v3d& p);

struct StratumRange
{
    uint32_t offset_ = 0;
    uint32_t count_ = 0;
};

// cells + 1 vertices a side, row-major, j * (cells + 1) + i, relative to
// center_. A parent (grandparent) position is where the vertex sits on the
// parent's (grandparent's) triangles: the morph targets.
struct ChunkMesh
{
    int32_t cells_ = kChunkCells;
    v3d center_ = v3d::Zero();
    std::vector<v3f> positions_;
    std::vector<v3f> normals_;
    std::vector<v3f> parentPositions_;
    std::vector<v3f> parentNormals_;
    std::vector<v3f> grandparentPositions_;
    std::vector<v3f> grandparentNormals_;
    // Per triangle, in grid order.
    std::vector<uint8_t> strata_;
    // Sorted by stratum.
    std::vector<uint16_t> indices_;
    std::array<StratumRange, kMaxTerrainStrata> ranges_{};
};

ChunkMesh buildChunk(const TerrainRelief& relief, const TerrainGrid& grid, const ChunkKey& key);

uint32_t packNormal(const v3f& n);
v3f unpackNormal(uint32_t packed);
std::vector<TerrainVertexGPUData> packVertices(const ChunkMesh& mesh);

}  // namespace batap
