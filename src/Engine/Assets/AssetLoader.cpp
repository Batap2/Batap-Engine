#include "AssetLoader.h"

#include "AssetManager.h"
#include "Context.h"
#include "Mesh.h"
#include "Renderer/ResourceManager.h"
#include "Serialization/BmeshSerializer.h"
#include "Utils/UIDGenerator.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace batap
{

static std::string_view extractExtension(std::string_view path)
{
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 >= path.size())
        return {};
    return path.substr(dot + 1);
}

static std::optional<AssetHandleAny> loadMesh(std::string_view relPath, const Context& ctx)
{
    auto& assetManager = *ctx._assetManager;
    assert(!assetManager.baseDir().empty() &&
           "AssetManager baseDir not set — call setBaseDir before loading assets");
    const std::string absPath = (std::filesystem::path(assetManager.baseDir()) / relPath).string();

    auto data = readBmesh(absPath);
    if (!data || data->vertices.empty())
    {
        std::cerr << "[AssetLoader] Failed to read bmesh: " << absPath << "\n";
        return std::nullopt;
    }

    const std::string key = std::string(relPath);
    auto [handle, inserted] = assetManager.emplace<Mesh>(key, key);
    if (!inserted)
        return AssetHandleAny{handle};

    auto* mesh = assetManager.get(handle);
    auto* rm = assetManager.resourceManager_;
    const std::string prefix = key + "_" + std::to_string(next_uid64());

    auto rname = [&](const char* suffix) { return prefix + suffix; };

    {
        const auto bufSize = sizeof(uint32_t) * data->indices.size();
        const auto guid = rm->createBufferStaticResource(bufSize, D3D12_RESOURCE_STATE_COPY_DEST,
                                                         D3D12_HEAP_TYPE_DEFAULT, rname("_i"));
        mesh->_indexBuffer =
            rm->createStaticIBV(guid, ResourceFormat::R32_UINT, rname("_iv"), 0, bufSize);
        auto span = rm->requestUploadOwned(guid, bufSize, 4);
        std::memcpy(span.data(), data->indices.data(), bufSize);
        mesh->_indexCount = static_cast<uint32_t>(data->indices.size());
    }
    {
        const auto bufSize = sizeof(v3f) * data->vertices.size();
        const auto guid = rm->createBufferStaticResource(bufSize, D3D12_RESOURCE_STATE_COPY_DEST,
                                                         D3D12_HEAP_TYPE_DEFAULT, rname("_v"));
        mesh->_vertexBuffer = rm->createStaticVBV(guid, sizeof(v3f), rname("_vv"), 0, bufSize);
        auto span = rm->requestUploadOwned(guid, bufSize, 0);
        std::memcpy(span.data(), data->vertices.data(), bufSize);
        mesh->_vertexCount = static_cast<uint32_t>(data->vertices.size());
    }
    if (!data->normals.empty())
    {
        const auto bufSize = sizeof(v3f) * data->normals.size();
        const auto guid = rm->createBufferStaticResource(bufSize, D3D12_RESOURCE_STATE_COPY_DEST,
                                                         D3D12_HEAP_TYPE_DEFAULT, rname("_n"));
        mesh->_normalBuffer = rm->createStaticVBV(guid, sizeof(v3f), rname("_nv"), 0, bufSize);
        auto span = rm->requestUploadOwned(guid, bufSize, 0);
        std::memcpy(span.data(), data->normals.data(), bufSize);
    }
    if (!data->uvs.empty())
    {
        const auto bufSize = sizeof(v2f) * data->uvs.size();
        const auto guid = rm->createBufferStaticResource(bufSize, D3D12_RESOURCE_STATE_COPY_DEST,
                                                         D3D12_HEAP_TYPE_DEFAULT, rname("_uv"));
        mesh->_uv0Buffer = rm->createStaticVBV(guid, sizeof(v2f), rname("_uvv"), 0, bufSize);
        auto span = rm->requestUploadOwned(guid, bufSize, 0);
        std::memcpy(span.data(), data->uvs.data(), bufSize);
    }

    mesh->_indexFormat = ResourceFormat::R32_UINT;
    mesh->subMeshCount = static_cast<uint8_t>(std::min(data->subMeshes.size(), size_t(8)));
    for (uint8_t i = 0; i < mesh->subMeshCount; ++i)
        mesh->subMeshes[i] = {data->subMeshes[i].indexOffset, data->subMeshes[i].indexCount};

    return AssetHandleAny{handle};
}

std::optional<AssetHandleAny> loadAsset(std::string_view path, const Context& ctx)
{
    const auto ext = extractExtension(path);

    if (ext == "bmesh")
        return loadMesh(path, ctx);

    // .png / .jpg / .jpeg → Texture (not yet implemented)

    return std::nullopt;
}

}  // namespace batap
