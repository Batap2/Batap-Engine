#include "AssetLoader.h"

#include "AssetManager.h"
#include "Engine.h"
#include "Material.h"
#include "Mesh.h"
#include "Texture.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/SkyIrradiance.h"
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

static std::optional<AssetHandleAny> loadMesh(std::string_view relPath, const Engine& ctx)
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

    // Compute tangents if normals and UVs are present.
    // Stored as float4: xyz = tangent direction (world-space ready), w = handedness (±1).
    if (!data->normals.empty() && !data->uvs.empty())
    {
        const size_t vcount = data->vertices.size();
        std::vector<v3f> tanSum(vcount, v3f::Zero());
        std::vector<v3f> biSum(vcount, v3f::Zero());

        for (size_t i = 0; i + 2 < data->indices.size(); i += 3)
        {
            const uint32_t i0 = data->indices[i];
            const uint32_t i1 = data->indices[i + 1];
            const uint32_t i2 = data->indices[i + 2];

            const v3f e1  = data->vertices[i1] - data->vertices[i0];
            const v3f e2  = data->vertices[i2] - data->vertices[i0];
            const v2f d1  = data->uvs[i1] - data->uvs[i0];
            const v2f d2  = data->uvs[i2] - data->uvs[i0];

            const float det = d1.x() * d2.y() - d1.y() * d2.x();
            if (std::abs(det) < 1e-8f) continue;
            const float f = 1.0f / det;

            const v3f T = (e1 * d2.y() - e2 * d1.y()) * f;
            const v3f B = (e2 * d1.x() - e1 * d2.x()) * f;

            tanSum[i0] += T; tanSum[i1] += T; tanSum[i2] += T;
            biSum[i0]  += B; biSum[i1]  += B; biSum[i2]  += B;
        }

        std::vector<v4f> tangents(vcount);
        for (size_t i = 0; i < vcount; ++i)
        {
            const v3f N = data->normals[i].normalized();
            // Gram-Schmidt orthogonalize
            v3f T = (tanSum[i] - N * N.dot(tanSum[i]));
            const float tlen = T.norm();
            T = (tlen > 1e-6f) ? v3f(T / tlen) : v3f::UnitX();
            // Handedness: +1 if B is consistent with cross(N, T)
            const float w = (N.cross(T).dot(biSum[i]) < 0.0f) ? -1.0f : 1.0f;
            tangents[i] = v4f(T.x(), T.y(), T.z(), w);
        }

        const auto bufSize = sizeof(v4f) * vcount;
        const auto guid = rm->createBufferStaticResource(bufSize, D3D12_RESOURCE_STATE_COPY_DEST,
                                                         D3D12_HEAP_TYPE_DEFAULT, rname("_tan"));
        mesh->_tangeantBuffer = rm->createStaticVBV(guid, sizeof(v4f), rname("_tanv"), 0, bufSize);
        auto span = rm->requestUploadOwned(guid, bufSize, 0);
        std::memcpy(span.data(), tangents.data(), bufSize);
    }

    mesh->_indexFormat = ResourceFormat::R32_UINT;
    mesh->subMeshCount = static_cast<uint8_t>(std::min(data->subMeshes.size(), size_t(8)));
    for (uint8_t i = 0; i < mesh->subMeshCount; ++i)
        mesh->subMeshes[i] = {data->subMeshes[i].indexOffset, data->subMeshes[i].indexCount};

    return AssetHandleAny{handle};
}

static std::optional<AssetHandleAny> loadTexture(std::string_view relPath, const Engine& ctx,
                                                  bool isBtex)
{
    namespace fs = std::filesystem;
    auto& assetManager = *ctx._assetManager;
    const std::string key = std::string(relPath);

    // Early-out: already loaded, no GPU work needed.
    if (auto existing = assetManager.getHandle<Texture>(key))
        return AssetHandleAny{*existing};

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

    // decode image — force RGBA; HDR files use float per channel
    const bool isHdr = (extractExtension(desc.sourcePath) == "hdr");
    int w, h, channels;
    float*    hdrPixels = nullptr;
    uint8_t*  ldrPixels = nullptr;

    if (isHdr)
        hdrPixels = stbi_loadf(absSource.c_str(), &w, &h, &channels, 4);
    else
        ldrPixels = stbi_load(absSource.c_str(), &w, &h, &channels, 4);

    if (!hdrPixels && !ldrPixels)
    {
        std::cerr << "[AssetLoader] stbi_load failed: " << absSource << " — " << stbi_failure_reason() << "\n";
        return std::nullopt;
    }

    const DXGI_FORMAT    gpuFmt        = isHdr ? DXGI_FORMAT_R32G32B32A32_FLOAT
                                                : DXGI_FORMAT_R8G8B8A8_UNORM;
    const uint32_t       bytesPerPixel = isHdr ? 16u : 4u;
    const ResourceFormat resFmt        = isHdr ? ResourceFormat::R32G32B32A32_FLOAT
                                                : ResourceFormat::R8G8B8A8_UNORM;

    auto* rm = assetManager.resourceManager_;
    const std::string prefix = key + "_" + std::to_string(next_uid64());

    // GPU resource
    const auto resHandle = rm->createTexture2DStaticResource(
        static_cast<uint32_t>(w), static_cast<uint32_t>(h),
        gpuFmt,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_HEAP_TYPE_DEFAULT,
        prefix + "_tex");

    // SRV — allocates a bindless slot in the descriptor heap
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                        = gpuFmt;
    srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels           = 1;
    auto viewHandle = rm->createStaticView<D3D12_SHADER_RESOURCE_VIEW_DESC>(resHandle, srvDesc,
                                                                             prefix + "_srv");

    // upload rows with D3D12 pitch alignment
    uint32_t rowPitch = 0;
    auto span = rm->requestTextureUploadOwned(resHandle, static_cast<uint32_t>(w),
                                              static_cast<uint32_t>(h), gpuFmt, rowPitch);
    const uint32_t srcRowPitch = static_cast<uint32_t>(w) * bytesPerPixel;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    const uint8_t* srcBytes = isHdr ? reinterpret_cast<const uint8_t*>(hdrPixels) : ldrPixels;
    for (uint32_t y = 0; y < static_cast<uint32_t>(h); ++y)
        std::memcpy(span.data() + y * rowPitch, srcBytes + y * srcRowPitch, srcRowPitch);
#pragma clang diagnostic pop

    // Projection SH pour les textures HDR (avant libération des pixels CPU)
    SH9 hdriSH;
    if (isHdr && hdrPixels)
        hdriSH = projectHDRIToSH(hdrPixels, w, h);

    stbi_image_free(isHdr ? static_cast<void*>(hdrPixels) : static_cast<void*>(ldrPixels));

    // build runtime Texture
    Texture tex{};
    tex.viewHandle_ = viewHandle;
    tex.heapIdx_    = rm->getStaticView(viewHandle)._descriptorHandle->heapIdx;
    tex.format_     = resFmt;
    tex.colorSpace_    = isHdr ? TextureColorSpace::Linear : TextureColorSpace::SRGB;
    tex.sizeX_         = static_cast<uint32_t>(w);
    tex.sizeY_         = static_cast<uint32_t>(h);
    tex.irradianceSH_  = hdriSH;

    const std::string name  = std::filesystem::path(relPath).stem().string();
    auto [handle, _] = assetManager.emplace<Texture>(name, key, tex);
    return AssetHandleAny{handle};
}

static std::optional<AssetHandleAny> loadMaterial(std::string_view relPath, const Engine& ctx)
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
    auto resolveTexIdx = [&](const std::string& texPath,
                             const std::string& fallbackKey = "__default_white") -> uint32_t {
        auto* fallbackTex       = assetManager.get<Texture>(fallbackKey);
        const uint32_t fallback = fallbackTex ? fallbackTex->heapIdx_ : 0xFFFFFFFFu;
        if (texPath.empty()) return fallback;
        const bool isBtex = (extractExtension(texPath) == "btex");
        if (!loadTexture(texPath, ctx, isBtex)) return fallback;
        auto* tex = assetManager.get<Texture>(texPath);
        return tex ? tex->heapIdx_ : fallback;
    };

    mat.albedoTexIdx_    = resolveTexIdx(data->albedoTexPath);
    mat.normalTexIdx_    = resolveTexIdx(data->normalTexPath, "__default_flat_normal");
    mat.roughnessTexIdx_ = resolveTexIdx(data->roughnessTexPath);
    mat.metallicTexIdx_  = resolveTexIdx(data->metallicTexPath);

    const std::string key  = std::string(relPath);
    const std::string name = fs::path(relPath).stem().string();
    auto [handle, inserted] = assetManager.emplace<Material>(name, key, mat);
    return AssetHandleAny{handle};
}

void createDefaultAssets(const Engine& ctx)
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

    // ---- 1×1 flat normal texture (128, 128, 255) → tangent-space (0,0,1) ----
    static constexpr uint8_t kFlatNormal[4] = {128, 128, 255, 255};
    const std::string flatNrmKey    = "__default_flat_normal";
    const std::string flatNrmPrefix = flatNrmKey + "_" + std::to_string(next_uid64());

    const auto flatResHandle = rm->createTexture2DStaticResource(
        1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_DEFAULT, flatNrmPrefix + "_tex");

    D3D12_SHADER_RESOURCE_VIEW_DESC flatSrvDesc{};
    flatSrvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    flatSrvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    flatSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    flatSrvDesc.Texture2D.MipLevels     = 1;
    auto flatViewHandle = rm->createStaticView<D3D12_SHADER_RESOURCE_VIEW_DESC>(
        flatResHandle, flatSrvDesc, flatNrmPrefix + "_srv");

    uint32_t flatRowPitch = 0;
    auto flatSpan = rm->requestTextureUploadOwned(flatResHandle, 1, 1,
                                                  DXGI_FORMAT_R8G8B8A8_UNORM, flatRowPitch);
    std::memcpy(flatSpan.data(), kFlatNormal, 4);

    Texture flatNrmTex{};
    flatNrmTex.viewHandle_ = flatViewHandle;
    flatNrmTex.heapIdx_    = rm->getStaticView(flatViewHandle)._descriptorHandle->heapIdx;
    flatNrmTex.format_     = ResourceFormat::R8G8B8A8_UNORM;
    flatNrmTex.sizeX_      = 1;
    flatNrmTex.sizeY_      = 1;
    assetManager.emplace<Texture>("__default_flat_normal", flatNrmKey, flatNrmTex);

    // ---- Default material — slot 0 in the GPU arena ----
    // Albedo/roughness/metallic → white (neutral ×1.0 multiplier).
    // Normal → flat normal (0,0,1 in tangent space = vertex normal passthrough).
    const uint32_t whiteIdx   = whiteTex.heapIdx_;
    const uint32_t flatNrmIdx = flatNrmTex.heapIdx_;
    Material defMat{};
    defMat.albedo[0]        = 0.9f;
    defMat.albedo[1]        = 0.9f;
    defMat.albedo[2]        = 0.9f;
    defMat.albedo[3]        = 1.0f;
    defMat.roughness        = 0.5f;
    defMat.metallic         = 0.0f;
    defMat.albedoTexIdx_    = whiteIdx;
    defMat.normalTexIdx_    = flatNrmIdx;
    defMat.roughnessTexIdx_ = whiteIdx;
    defMat.metallicTexIdx_  = whiteIdx;
    assetManager.emplace<Material>("__default_material", "__default_material", defMat);
}

std::optional<AssetHandleAny> loadAsset(std::string_view path, const Engine& ctx)
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
