#include "AssetLoader.h"

#include "AssetManager.h"
#include "Context.h"
#include "Material.h"
#include "Mesh.h"
#include "Texture.h"
#include "Renderer/ResourceManager.h"
#include "Serialization/BmatSerializer.h"
#include "Serialization/BmeshSerializer.h"
#include "Serialization/BtexSerializer.h"
#include "Utils/UIDGenerator.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

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

static std::optional<AssetHandleAny> loadTexture(std::string_view relPath, const Context& ctx,
                                                  bool isBtex)
{
    namespace fs = std::filesystem;
    auto& assetManager = *ctx._assetManager;
    const std::string key = std::string(relPath);

    // resolve source image path — .btex redirects to its sourcePath
    TextureDesc desc;
    if (isBtex)
    {
        const std::string absBtex = (fs::path(assetManager.baseDir()) / relPath).string();
        auto d = readBtex(absBtex);
        if (!d)
        {
            std::cerr << "[AssetLoader] Failed to read btex: " << absBtex << "\n";
            return std::nullopt;
        }
        desc = std::move(*d);
    }
    else
    {
        desc.sourcePath = key;
    }

    const std::string absSource = (fs::path(assetManager.baseDir()) / desc.sourcePath).string();

    // decode image — force RGBA
    int w, h, channels;
    uint8_t* pixels = stbi_load(absSource.c_str(), &w, &h, &channels, 4);
    if (!pixels)
    {
        std::cerr << "[AssetLoader] stbi_load failed: " << absSource << " — " << stbi_failure_reason() << "\n";
        return std::nullopt;
    }

    auto* rm = assetManager.resourceManager_;
    const std::string prefix = key + "_" + std::to_string(next_uid64());

    // GPU resource
    const auto resHandle = rm->createTexture2DStaticResource(
        static_cast<uint32_t>(w), static_cast<uint32_t>(h),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_HEAP_TYPE_DEFAULT,
        prefix + "_tex");

    // SRV — allocates a bindless slot in the descriptor heap
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                        = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels           = 1;
    auto viewHandle = rm->createStaticView<D3D12_SHADER_RESOURCE_VIEW_DESC>(resHandle, srvDesc,
                                                                             prefix + "_srv");

    // upload rows with D3D12 pitch alignment
    uint32_t rowPitch = 0;
    auto span = rm->requestTextureUploadOwned(resHandle, static_cast<uint32_t>(w),
                                              static_cast<uint32_t>(h),
                                              DXGI_FORMAT_R8G8B8A8_UNORM, rowPitch);
    const uint32_t srcRowPitch = static_cast<uint32_t>(w) * 4;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    for (uint32_t y = 0; y < static_cast<uint32_t>(h); ++y)
        std::memcpy(span.data() + y * rowPitch, pixels + y * srcRowPitch, srcRowPitch);
#pragma clang diagnostic pop

    stbi_image_free(pixels);

    // build runtime Texture
    Texture tex{};
    tex.viewHandle_ = viewHandle;
    tex.heapIdx_    = rm->getStaticView(viewHandle)._descriptorHandle->heapIdx;
    tex.format_     = ResourceFormat::R8G8B8A8_UNORM;
    tex.sizeX_      = static_cast<uint32_t>(w);
    tex.sizeY_      = static_cast<uint32_t>(h);

    const std::string name  = std::filesystem::path(relPath).stem().string();
    auto [handle, inserted] = assetManager.emplace<Texture>(name, key, tex);
    if (!inserted)
    {
        // already loaded — release the GPU resources we just created
        rm->requestDestroy(viewHandle, /*destroyAssociatedResources=*/true);
    }
    return AssetHandleAny{handle};
}

static std::optional<AssetHandleAny> loadMaterial(std::string_view relPath, const Context& ctx)
{
    namespace fs = std::filesystem;
    auto& assetManager = *ctx._assetManager;
    const std::string absPath = (fs::path(assetManager.baseDir()) / relPath).string();

    auto data = readBmat(absPath);
    if (!data)
    {
        std::cerr << "[AssetLoader] Failed to read bmat: " << absPath << "\n";
        return std::nullopt;
    }

    Material mat = data->mat;

    // Resolve texture paths → bindless heapIdx.
    // Falls back to white texture (neutral multiplier) when path is empty or load fails.
    auto resolveTexIdx = [&](const std::string& texPath) -> uint32_t {
        auto* white         = assetManager.get<Texture>(std::string("__default_white"));
        const uint32_t fallback = white ? white->heapIdx_ : 0xFFFFFFFFu;
        if (texPath.empty()) return fallback;
        const bool isBtex = (extractExtension(texPath) == "btex");
        if (!loadTexture(texPath, ctx, isBtex)) return fallback;
        auto* tex = assetManager.get<Texture>(texPath);
        return tex ? tex->heapIdx_ : fallback;
    };

    mat.albedoTexIdx_    = resolveTexIdx(data->albedoTexPath);
    mat.normalTexIdx_    = resolveTexIdx(data->normalTexPath);
    mat.roughnessTexIdx_ = resolveTexIdx(data->roughnessTexPath);
    mat.metallicTexIdx_  = resolveTexIdx(data->metallicTexPath);

    const std::string key  = std::string(relPath);
    const std::string name = fs::path(relPath).stem().string();
    auto [handle, inserted] = assetManager.emplace<Material>(name, key, mat);
    return AssetHandleAny{handle};
}

void createDefaultAssets(const Context& ctx)
{
    auto& assetManager = *ctx._assetManager;
    auto* rm           = assetManager.resourceManager_;

    // ---- 1×1 white RGBA texture ----
    static constexpr uint8_t kWhite[4] = {255, 255, 255, 255};
    const std::string texKey            = "__default_white";
    const std::string prefix            = texKey + "_" + std::to_string(next_uid64());

    const auto resHandle = rm->createTexture2DStaticResource(
        1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_DEFAULT, prefix + "_tex");

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;
    auto viewHandle =
        rm->createStaticView<D3D12_SHADER_RESOURCE_VIEW_DESC>(resHandle, srvDesc, prefix + "_srv");

    uint32_t rowPitch = 0;
    auto span = rm->requestTextureUploadOwned(resHandle, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, rowPitch);
    std::memcpy(span.data(), kWhite, 4);

    Texture whiteTex{};
    whiteTex.viewHandle_ = viewHandle;
    whiteTex.heapIdx_    = rm->getStaticView(viewHandle)._descriptorHandle->heapIdx;
    whiteTex.format_     = ResourceFormat::R8G8B8A8_UNORM;
    whiteTex.sizeX_      = 1;
    whiteTex.sizeY_      = 1;
    assetManager.emplace<Texture>("__default_white", texKey, whiteTex);

    // ---- Default material — slot 0 in the GPU arena ----
    // All tex channels point to white (neutral multiplier: value * 1.0 = value).
    // When using actual textures, set the material's base roughness/metallic to 1.0
    // so the texture drives the final value directly.
    const uint32_t whiteIdx = whiteTex.heapIdx_;
    Material defMat{};
    defMat.albedo[0]        = 0.9f;
    defMat.albedo[1]        = 0.9f;
    defMat.albedo[2]        = 0.9f;
    defMat.albedo[3]        = 1.0f;
    defMat.roughness        = 0.5f;
    defMat.metallic         = 0.0f;
    defMat.albedoTexIdx_    = whiteIdx;
    defMat.normalTexIdx_    = whiteIdx;
    defMat.roughnessTexIdx_ = whiteIdx;
    defMat.metallicTexIdx_  = whiteIdx;
    assetManager.emplace<Material>("__default_material", "__default_material", defMat);
}

std::optional<AssetHandleAny> loadAsset(std::string_view path, const Context& ctx)
{
    const auto ext = extractExtension(path);

    if (ext == "bmesh") return loadMesh(path, ctx);
    if (ext == "bmat")  return loadMaterial(path, ctx);
    if (ext == "btex")  return loadTexture(path, ctx, true);

    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "hdr")
        return loadTexture(path, ctx, false);

    return std::nullopt;
}

}  // namespace batap
