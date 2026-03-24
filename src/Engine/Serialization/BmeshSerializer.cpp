#include "BmeshSerializer.h"

#include <fstream>
#include <iostream>

namespace batap
{

bool writeBmesh(const BmeshData& data, const std::string& outPath)
{
    std::ofstream f(outPath, std::ios::binary);
    if (!f.is_open())
    {
        std::cerr << "[BmeshSerializer] Cannot open for write: " << outPath << "\n";
        return false;
    }

    uint32_t flags = 0;
    if (!data.normals.empty())
        flags |= BMESH_FLAG_NORMALS;
    if (!data.uvs.empty())
        flags |= BMESH_FLAG_UVS;

    const uint32_t vertexCount = static_cast<uint32_t>(data.vertices.size());
    const uint32_t indexCount = static_cast<uint32_t>(data.indices.size());

    const uint32_t subMeshCount = static_cast<uint32_t>(data.subMeshes.size());

    f.write(reinterpret_cast<const char*>(&BMESH_MAGIC), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&BMESH_VERSION), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&vertexCount), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&indexCount), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&flags), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(&subMeshCount), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(data.subMeshes.data()),
            sizeof(SubMeshData) * subMeshCount);

    f.write(reinterpret_cast<const char*>(data.indices.data()), sizeof(uint32_t) * indexCount);
    f.write(reinterpret_cast<const char*>(data.vertices.data()), sizeof(v3f) * vertexCount);

    if (flags & BMESH_FLAG_NORMALS)
        f.write(reinterpret_cast<const char*>(data.normals.data()), sizeof(v3f) * vertexCount);

    if (flags & BMESH_FLAG_UVS)
        f.write(reinterpret_cast<const char*>(data.uvs.data()), sizeof(v2f) * vertexCount);

    return f.good();
}

std::optional<BmeshData> readBmesh(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
    {
        std::cerr << "[BmeshSerializer] Cannot open for read: " << path << "\n";
        return std::nullopt;
    }

    uint32_t magic, version, vertexCount, indexCount, flags, subMeshCount;
    f.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&vertexCount), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&indexCount), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&flags), sizeof(uint32_t));
    f.read(reinterpret_cast<char*>(&subMeshCount), sizeof(uint32_t));

    if (magic != BMESH_MAGIC)
    {
        std::cerr << "[BmeshSerializer] Invalid magic: " << path << "\n";
        return std::nullopt;
    }
    if (version != BMESH_VERSION)
    {
        std::cerr << "[BmeshSerializer] Unsupported version " << version << ": " << path << "\n";
        return std::nullopt;
    }

    BmeshData data;

    data.subMeshes.resize(subMeshCount);
    f.read(reinterpret_cast<char*>(data.subMeshes.data()), sizeof(SubMeshData) * subMeshCount);

    data.indices.resize(indexCount);
    f.read(reinterpret_cast<char*>(data.indices.data()), sizeof(uint32_t) * indexCount);

    data.vertices.resize(vertexCount);
    f.read(reinterpret_cast<char*>(data.vertices.data()), sizeof(v3f) * vertexCount);

    if (flags & BMESH_FLAG_NORMALS)
    {
        data.normals.resize(vertexCount);
        f.read(reinterpret_cast<char*>(data.normals.data()), sizeof(v3f) * vertexCount);
    }

    if (flags & BMESH_FLAG_UVS)
    {
        data.uvs.resize(vertexCount);
        f.read(reinterpret_cast<char*>(data.uvs.data()), sizeof(v2f) * vertexCount);
    }

    if (!f.good())
    {
        std::cerr << "[BmeshSerializer] Read error: " << path << "\n";
        return std::nullopt;
    }

    return data;
}

}  // namespace batap
