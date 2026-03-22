#include "AssetLoader.h"

#include "AssetManager.h"
#include "Context.h"
#include "Mesh.h"
#include "Renderer/ResourceManager.h"
#include "Serialization/BmeshSerializer.h"
#include "Utils/UIDGenerator.h"

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

static std::optional<AssetHandleAny> loadMesh(std::string_view path, const Context& ctx)
{
    auto data = readBmesh(std::string(path));
    if (!data || data->vertices.empty())
    {
        std::cerr << "[AssetLoader] Failed to read bmesh: " << path << "\n";
        return std::nullopt;
    }

    auto& assetManager = *ctx._assetManager;
    const std::string name = std::filesystem::path(path).stem().string();
    auto [handle, inserted] = assetManager.emplace<Mesh>(name, std::string(path));
    if (!inserted)
        return AssetHandleAny{handle};

    auto* mesh = assetManager.get(handle);
    auto* rm = assetManager.resourceManager_;
    const std::string prefix = name + "_" + std::to_string(next_uid64());

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
