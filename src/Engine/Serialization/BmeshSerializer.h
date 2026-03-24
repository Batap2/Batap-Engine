#pragma once

#include "EigenTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace batap
{

static constexpr uint32_t BMESH_MAGIC = 0x48534D42;  // 'BMSH'
static constexpr uint32_t BMESH_VERSION = 1;

static constexpr uint32_t BMESH_FLAG_NORMALS = 1 << 0;
static constexpr uint32_t BMESH_FLAG_UVS = 1 << 1;

struct SubMeshData
{
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
};

struct BmeshData
{
    std::vector<uint32_t> indices;
    std::vector<v3f> vertices;
    std::vector<v3f> normals;
    std::vector<v2f> uvs;
    std::vector<SubMeshData> subMeshes;
};

bool writeBmesh(const BmeshData& data, const std::string& outPath);
std::optional<BmeshData> readBmesh(const std::string& path);

}  // namespace batap
