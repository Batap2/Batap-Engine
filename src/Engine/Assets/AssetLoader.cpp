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
    auto& assetManager = *ctx.assetManager_;
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

    // All the streams of a mesh share one buffer, each at its own offset.
    // 16 bytes covers the index-buffer offset requirement (a multiple of the
    // index size) and every attribute format.
    const size_t vcount = data->vertices.size();

    const bool sourceHasNormals = !data->normals.empty();
    const bool sourceHasUVs = !data->uvs.empty();
    if (!sourceHasNormals)
        data->normals.assign(vcount, v3f::UnitY());
    if (!sourceHasUVs)
        data->uvs.assign(vcount, v2f::Zero());

    uint64_t cursor = 0;
    auto place = [&cursor](size_t bytes)
    {
        const uint64_t offset = (cursor + 15) & ~uint64_t(15);
        cursor = offset + bytes;
        return offset;
    };

    const size_t indexBytes = sizeof(uint32_t) * data->indices.size();
    const size_t vertexBytes = sizeof(v3f) * vcount;
    const size_t normalBytes = sizeof(v3f) * vcount;
    const size_t uvBytes = sizeof(v2f) * vcount;
    const size_t tangentBytes = sizeof(v4f) * vcount;

    const uint64_t indexOffset = place(indexBytes);
    const uint64_t vertexOffset = place(vertexBytes);
    const uint64_t normalOffset = place(normalBytes);
    const uint64_t uvOffset = place(uvBytes);
    const uint64_t tangentOffset = place(tangentBytes);

    const auto guid =
        rm->createStaticBuffer(cursor, key + "_" + std::to_string(next_uid64()) + "_mesh");

    mesh->buffer_ = guid;
    mesh->streamOffsets_[Mesh::Index] = indexOffset;
    mesh->indexCount_ = static_cast<uint32_t>(data->indices.size());
    std::memcpy(rm->requestUpload(guid, indexBytes, indexOffset).data(), data->indices.data(),
                indexBytes);

    mesh->streamOffsets_[Mesh::Position] = vertexOffset;
    mesh->vertexCount_ = static_cast<uint32_t>(vcount);
    std::memcpy(rm->requestUpload(guid, vertexBytes, vertexOffset).data(), data->vertices.data(),
                vertexBytes);

    mesh->streamOffsets_[Mesh::Normal] = normalOffset;
    std::memcpy(rm->requestUpload(guid, normalBytes, normalOffset).data(), data->normals.data(),
                normalBytes);

    mesh->streamOffsets_[Mesh::UV0] = uvOffset;
    std::memcpy(rm->requestUpload(guid, uvBytes, uvOffset).data(), data->uvs.data(), uvBytes);

    // Tangents need real UVs to mean anything; without them the value is unused
    // (no UVs means no normal map) and only has to stay well-formed.
    // Stored as float4: xyz = tangent direction (world-space ready), w = handedness (±1).
    std::vector<v4f> tangents(vcount, v4f(1.f, 0.f, 0.f, 1.f));
    if (sourceHasNormals && sourceHasUVs)
    {
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
    }

    mesh->streamOffsets_[Mesh::Tangent] = tangentOffset;
    std::memcpy(rm->requestUpload(guid, tangentBytes, tangentOffset).data(), tangents.data(),
                tangentBytes);

    mesh->indexFormat_ = ResourceFormat::R32_UINT;
    mesh->subMeshCount = static_cast<uint8_t>(std::min(data->subMeshes.size(), size_t(8)));
    for (uint8_t i = 0; i < mesh->subMeshCount; ++i)
        mesh->subMeshes[i] = {data->subMeshes[i].indexOffset, data->subMeshes[i].indexCount};

    return AssetHandleAny{handle};
}

static std::optional<AssetHandleAny> loadTexture(std::string_view relPath, const Engine& ctx,
                                                  bool isBtex)
{
    namespace fs = std::filesystem;
    auto& assetManager = *ctx.assetManager_;
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

    const uint32_t       bytesPerPixel = isHdr ? 16u : 4u;
    const ResourceFormat resFmt        = isHdr ? ResourceFormat::R32G32B32A32_FLOAT
                                                : ResourceFormat::R8G8B8A8_UNORM;

    auto* rm = assetManager.resourceManager_;
    const std::string prefix = key + "_" + std::to_string(next_uid64());

    // GPU resource + SRV (slot bindless)
    const auto gpuTex = rm->createImage2D(static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                            resFmt, prefix + "_tex");

    // The staging span is tightly packed, same layout as the decoded pixels
    auto span = rm->requestTextureUpload(gpuTex, static_cast<uint32_t>(w),
                                              static_cast<uint32_t>(h), resFmt);
    const void* srcBytes = isHdr ? static_cast<const void*>(hdrPixels)
                                 : static_cast<const void*>(ldrPixels);
    std::memcpy(span.data(), srcBytes, span.size());

    // Projection SH pour les textures HDR (avant libération des pixels CPU)
    SH9 hdriSH;
    if (isHdr && hdrPixels)
        hdriSH = projectHDRIToSH(hdrPixels, w, h);

    stbi_image_free(isHdr ? static_cast<void*>(hdrPixels) : static_cast<void*>(ldrPixels));

    // build runtime Texture
    Texture tex{};
    tex.bindlessIndex_    = rm->textureIndex(gpuTex);
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
    auto& assetManager = *ctx.assetManager_;
    const std::string absPath = (fs::path(assetManager.baseDir()) / relPath).string();

    auto data = readBmat(absPath);
    if (!data)
    {
        std::cerr << "[AssetLoader] Failed to read bmat: " << absPath << "\n";
        return std::nullopt;
    }

    Material mat = data->mat;

    // Resolve texture paths → bindless index.
    // Falls back to white texture (neutral multiplier) when path is empty or load fails.
    auto resolveTexIdx = [&](const std::string& texPath,
                             const std::string& fallbackKey = "__default_white") -> uint32_t {
        auto* fallbackTex       = assetManager.get<Texture>(fallbackKey);
        const uint32_t fallback = fallbackTex ? fallbackTex->bindlessIndex_ : 0xFFFFFFFFu;
        if (texPath.empty()) return fallback;
        const bool isBtex = (extractExtension(texPath) == "btex");
        if (!loadTexture(texPath, ctx, isBtex)) return fallback;
        auto* tex = assetManager.get<Texture>(texPath);
        return tex ? tex->bindlessIndex_ : fallback;
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
    auto& assetManager = *ctx.assetManager_;
    auto* rm           = assetManager.resourceManager_;

    // ---- 1×1 white RGBA texture ----
    static constexpr uint8_t kWhite[4] = {255, 255, 255, 255};
    const std::string texKey            = "__default_white";
    const std::string prefix            = texKey + "_" + std::to_string(next_uid64());

    const auto whiteGpu = rm->createImage2D(1, 1, ResourceFormat::R8G8B8A8_UNORM,
                                              prefix + "_tex");

    auto span = rm->requestTextureUpload(whiteGpu, 1, 1,
                                              ResourceFormat::R8G8B8A8_UNORM);
    std::memcpy(span.data(), kWhite, 4);

    Texture whiteTex{};
    whiteTex.bindlessIndex_    = rm->textureIndex(whiteGpu);
    whiteTex.format_     = ResourceFormat::R8G8B8A8_UNORM;
    whiteTex.sizeX_      = 1;
    whiteTex.sizeY_      = 1;
    assetManager.emplace<Texture>("__default_white", texKey, whiteTex);

    // ---- 1×1 flat normal texture (128, 128, 255) → tangent-space (0,0,1) ----
    static constexpr uint8_t kFlatNormal[4] = {128, 128, 255, 255};
    const std::string flatNrmKey    = "__default_flat_normal";
    const std::string flatNrmPrefix = flatNrmKey + "_" + std::to_string(next_uid64());

    const auto flatGpu = rm->createImage2D(1, 1, ResourceFormat::R8G8B8A8_UNORM,
                                             flatNrmPrefix + "_tex");

    auto flatSpan = rm->requestTextureUpload(flatGpu, 1, 1,
                                                  ResourceFormat::R8G8B8A8_UNORM);
    std::memcpy(flatSpan.data(), kFlatNormal, 4);

    Texture flatNrmTex{};
    flatNrmTex.bindlessIndex_    = rm->textureIndex(flatGpu);
    flatNrmTex.format_     = ResourceFormat::R8G8B8A8_UNORM;
    flatNrmTex.sizeX_      = 1;
    flatNrmTex.sizeY_      = 1;
    assetManager.emplace<Texture>("__default_flat_normal", flatNrmKey, flatNrmTex);

    // ---- Default material — slot 0 in the GPU arena ----
    // Albedo/roughness/metallic → white (neutral ×1.0 multiplier).
    // Normal → flat normal (0,0,1 in tangent space = vertex normal passthrough).
    const uint32_t whiteIdx   = whiteTex.bindlessIndex_;
    const uint32_t flatNrmIdx = flatNrmTex.bindlessIndex_;
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
