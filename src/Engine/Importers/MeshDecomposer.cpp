// un jour remplacer par :
// https://github.com/spnda/fastgltf?tab=readme-ov-file
// https://github.com/ufbx/ufbx
// mesh optimizer

#include "MeshDecomposer.h"

#include "Shaders/ShaderInterop.h"
#include "Assets/Texture.h"
#include "Serialization/BmatSerializer.h"
#include "Serialization/BmeshSerializer.h"
#include "Serialization/BtexSerializer.h"
#include "Serialization/EntityDesc.h"
#include "Serialization/EntitySerializer.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace batap
{

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"

// matCache: aiMaterial index -> .bmat path (relative to baseDir)
// imgCache: assimp texture ref ("tex.png" or "*0") -> source image path on disk
// texCache: source image path + color space -> .btex path (relative to baseDir)
struct ImportCaches
{
    std::unordered_map<uint32_t, std::string>    matCache;
    std::unordered_map<std::string, std::string> imgCache;
    std::unordered_map<std::string, std::string> texCache;
};

static std::string relToBase(const fs::path& p, std::string_view baseDir)
{
    std::error_code ec;
    const std::string rel = fs::relative(p, fs::path(baseDir), ec).generic_string();
    if (ec || rel.empty() || rel.starts_with(".."))
        return {};
    return rel;
}

static void pushRel(std::vector<std::string>& out, const fs::path& p, std::string_view baseDir)
{
    std::string rel = relToBase(p, baseDir);
    if (!rel.empty())
        out.push_back(std::move(rel));
}

static std::string sanitizeName(std::string name)
{
    for (char& c : name)
        if (c == '/' || c == '\\' || c == ':') c = '_';
    return name;
}

static BmeshData extractBmeshData(const aiMesh* mesh)
{
    BmeshData data;
    data.vertices.reserve(mesh->mNumVertices);

    for (unsigned i = 0; i < mesh->mNumVertices; ++i)
    {
        data.vertices.push_back({mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z});

        if (mesh->HasNormals())
            data.normals.push_back({mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z});

        if (mesh->HasTextureCoords(0))
            data.uvs.push_back({mesh->mTextureCoords[0][i].x, 1.0f - mesh->mTextureCoords[0][i].y});
    }

    data.indices.reserve(mesh->mNumFaces * 3);
    for (unsigned i = 0; i < mesh->mNumFaces; ++i)
    {
        data.indices.push_back(mesh->mFaces[i].mIndices[0]);
        data.indices.push_back(mesh->mFaces[i].mIndices[1]);
        data.indices.push_back(mesh->mFaces[i].mIndices[2]);
    }

    return data;
}

static BmeshData mergeNodeMeshes(const aiNode* node, const aiScene* scene)
{
    BmeshData merged;
    for (unsigned i = 0; i < node->mNumMeshes; ++i)
    {
        const aiMesh* m = scene->mMeshes[node->mMeshes[i]];
        BmeshData sub = extractBmeshData(m);

        const uint32_t indexOffset = static_cast<uint32_t>(merged.indices.size());
        const uint32_t vertexOffset = static_cast<uint32_t>(merged.vertices.size());

        for (uint32_t idx : sub.indices)
            merged.indices.push_back(idx + vertexOffset);

        merged.subMeshes.push_back({indexOffset, static_cast<uint32_t>(sub.indices.size())});

        merged.vertices.insert(merged.vertices.end(), sub.vertices.begin(), sub.vertices.end());
        merged.normals.insert(merged.normals.end(), sub.normals.begin(), sub.normals.end());
        merged.uvs.insert(merged.uvs.end(), sub.uvs.begin(), sub.uvs.end());
    }
    return merged;
}

static Material extractMaterial(const aiMaterial* aiMat)
{
    Material mat;
    aiColor4D color;
    if (aiMat->Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS ||
        aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
    {
        mat.albedo[0] = color.r;
        mat.albedo[1] = color.g;
        mat.albedo[2] = color.b;
        mat.albedo[3] = color.a;
    }
    float val;
    if (aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, val) == AI_SUCCESS) mat.roughness = val;
    if (aiMat->Get(AI_MATKEY_METALLIC_FACTOR,  val) == AI_SUCCESS) mat.metallic  = val;

    mat.reflectivity = 1.f;
    if (aiMat->Get(AI_MATKEY_SPECULAR_FACTOR, val) == AI_SUCCESS) mat.reflectivity = val;

    return mat;
}

static fs::path extractEmbeddedImage(const aiScene* scene, const std::string& rawPath,
                                     const fs::path& subDir)
{
    const int idx = std::atoi(rawPath.c_str() + 1);
    if (idx < 0 || static_cast<unsigned>(idx) >= scene->mNumTextures)
    {
        std::cerr << "[MeshDecomposer] Bad embedded texture ref: " << rawPath << "\n";
        return {};
    }
    const aiTexture* tex = scene->mTextures[static_cast<unsigned>(idx)];

    std::string name = tex->mFilename.length > 0 ? std::string(tex->mFilename.C_Str())
                                                 : std::string();
    name = sanitizeName(fs::path(name).stem().string());
    if (name.empty()) name = "embedded_" + std::to_string(idx);

    fs::create_directories(subDir);

    if (tex->mHeight == 0)
    {
        // Compressed blob: pcData holds the whole file, mWidth is its size in bytes.
        std::string ext;
        for (int i = 0; i < 4 && tex->achFormatHint[i] != '\0'; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(tex->achFormatHint[i]);
            if (std::isalnum(c)) ext += static_cast<char>(std::tolower(c));
        }
        if (ext.empty()) ext = "png";

        fs::path out = subDir / (name + "." + ext);
        std::ofstream f(out, std::ios::binary);
        if (!f)
        {
            std::cerr << "[MeshDecomposer] Cannot write embedded texture: " << out << "\n";
            return {};
        }
        f.write(reinterpret_cast<const char*>(tex->pcData),
                static_cast<std::streamsize>(tex->mWidth));
        return out;
    }

    // Uncompressed: mWidth x mHeight aiTexel, stored BGRA.
    const size_t count = static_cast<size_t>(tex->mWidth) * tex->mHeight;
    std::vector<unsigned char> rgba(count * 4);
    for (size_t i = 0; i < count; ++i)
    {
        rgba[i * 4 + 0] = tex->pcData[i].r;
        rgba[i * 4 + 1] = tex->pcData[i].g;
        rgba[i * 4 + 2] = tex->pcData[i].b;
        rgba[i * 4 + 3] = tex->pcData[i].a;
    }

    fs::path out = subDir / (name + ".png");
    if (!stbi_write_png(out.string().c_str(), static_cast<int>(tex->mWidth),
                        static_cast<int>(tex->mHeight), 4, rgba.data(),
                        static_cast<int>(tex->mWidth) * 4))
    {
        std::cerr << "[MeshDecomposer] Cannot write embedded texture: " << out << "\n";
        return {};
    }
    return out;
}

// Path on disk of the image feeding one texture slot, "" if the slot is empty or the file
// is missing. Embedded images are extracted once and reused.
static fs::path resolveSourceImage(const aiMaterial* aiMat, aiTextureType texType,
                                   const aiScene* scene, const fs::path& sourceDir,
                                   const fs::path& subDir, ImportCaches& caches)
{
    aiString aiTexPath;
    if (aiMat->GetTexture(texType, 0, &aiTexPath) != AI_SUCCESS || aiTexPath.length == 0)
        return {};

    const std::string rawPath = aiTexPath.C_Str();
    auto it = caches.imgCache.find(rawPath);
    if (it != caches.imgCache.end()) return fs::path(it->second);

    fs::path out;
    if (rawPath[0] == '*')
    {
        out = extractEmbeddedImage(scene, rawPath, subDir);
    }
    else
    {
        out = sourceDir / rawPath;
        if (!fs::exists(out))
        {
            std::cerr << "[MeshDecomposer] Texture not found: " << out << "\n";
            out.clear();
        }
    }

    caches.imgCache[rawPath] = out.string();
    return out;
}

// glTF packs metallic-roughness into one image (G = roughness, B = metallic) and assimp
// reports it on both slots. PixelShader.hlsl samples .r, so split it into two grayscale
// images rather than let both slots read the wrong channel.
static std::pair<fs::path, fs::path> splitPackedORM(const fs::path& src, const fs::path& subDir)
{
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(src.string().c_str(), &w, &h, &channels, 4);
    if (!pixels)
    {
        std::cerr << "[MeshDecomposer] Cannot decode packed metallic-roughness texture: "
                  << src << "\n";
        return {src, src};  // fall back to the packed image on both slots
    }

    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<unsigned char> rough(count), metal(count);
    for (size_t i = 0; i < count; ++i)
    {
        rough[i] = pixels[i * 4 + 1];  // G
        metal[i] = pixels[i * 4 + 2];  // B
    }
    stbi_image_free(pixels);

    fs::create_directories(subDir);
    const std::string stem = src.stem().string();
    fs::path roughPath = subDir / (stem + "_roughness.png");
    fs::path metalPath = subDir / (stem + "_metallic.png");
    stbi_write_png(roughPath.string().c_str(), w, h, 1, rough.data(), w);
    stbi_write_png(metalPath.string().c_str(), w, h, 1, metal.data(), w);
    return {roughPath, metalPath};
}

// Returns relative path to .btex (from baseDir), or "" for an empty slot. The color space
// is part of the key and of the file name: the same image used as both sRGB albedo and
// linear data needs two descriptors, not one overwriting the other.
static std::string writeBtexFor(const fs::path& srcImgPath, const fs::path& subDir,
                                std::string_view baseDir, TextureColorSpace colorSpace,
                                ImportCaches& caches)
{
    if (srcImgPath.empty()) return "";

    const bool        isLinear = (colorSpace == TextureColorSpace::Linear);
    const char*       suffix   = isLinear ? "_linear" : "";
    const std::string key = srcImgPath.generic_string() + (isLinear ? "|linear" : "|srgb");

    auto it = caches.texCache.find(key);
    if (it != caches.texCache.end()) return it->second;

    TextureDesc desc;
    desc.sourcePath = fs::relative(srcImgPath, baseDir).generic_string();
    desc.colorSpace = colorSpace;
    desc.filter     = TextureFilter::Linear;
    desc.wrapU      = TextureWrap::Repeat;
    desc.wrapV      = TextureWrap::Repeat;
    desc.mipLevels  = 0;  // auto full mip chain

    fs::path btexPath = subDir / (srcImgPath.stem().string() + suffix + ".btex");
    writeBtex(desc, btexPath.string());

    std::string rel      = fs::relative(btexPath, baseDir).generic_string();
    caches.texCache[key] = rel;
    return rel;
}

static std::string getOrWriteTexture(const aiMaterial* aiMat, aiTextureType texType,
                                     const aiScene* scene, const fs::path& sourceDir,
                                     const fs::path& subDir, std::string_view baseDir,
                                     TextureColorSpace colorSpace, ImportCaches& caches)
{
    const fs::path src = resolveSourceImage(aiMat, texType, scene, sourceDir, subDir, caches);
    return writeBtexFor(src, subDir, baseDir, colorSpace, caches);
}

static std::string getOrWriteMaterial(uint32_t matIdx, const aiScene* scene,
                                       const fs::path& subDir, std::string_view baseDir,
                                       const fs::path& sourceDir, ImportCaches& caches)
{
    auto it = caches.matCache.find(matIdx);
    if (it != caches.matCache.end()) return it->second;

    const aiMaterial* aiMat = scene->mMaterials[matIdx];
    aiString aiName;
    std::string name = (aiMat->Get(AI_MATKEY_NAME, aiName) == AI_SUCCESS && aiName.length > 0)
                           ? aiName.C_Str()
                           : ("mat_" + std::to_string(matIdx));
    name = sanitizeName(std::move(name));

    Material mat = extractMaterial(aiMat);
    MaterialDesc desc;
    desc.mat              = &mat;
    // glTF reports base color on BASE_COLOR, OBJ/FBX on DIFFUSE — assimp's glTF2 loader
    // fills both, so either order works there.
    desc.albedoTexPath    = getOrWriteTexture(aiMat, aiTextureType_BASE_COLOR, scene,
                                              sourceDir, subDir, baseDir,
                                              TextureColorSpace::SRGB, caches);
    if (desc.albedoTexPath.empty())
        desc.albedoTexPath = getOrWriteTexture(aiMat, aiTextureType_DIFFUSE, scene,
                                               sourceDir, subDir, baseDir,
                                               TextureColorSpace::SRGB, caches);
    // OBJ/MTL uses HEIGHT for bump maps; try NORMALS first then HEIGHT
    desc.normalTexPath    = getOrWriteTexture(aiMat, aiTextureType_NORMALS, scene,
                                              sourceDir, subDir, baseDir,
                                              TextureColorSpace::Linear, caches);
    if (desc.normalTexPath.empty())
        desc.normalTexPath = getOrWriteTexture(aiMat, aiTextureType_HEIGHT, scene,
                                               sourceDir, subDir, baseDir,
                                               TextureColorSpace::Linear, caches);

    // Same source image on both slots = glTF's packed metallicRoughness texture.
    const fs::path roughSrc = resolveSourceImage(aiMat, aiTextureType_DIFFUSE_ROUGHNESS,
                                                 scene, sourceDir, subDir, caches);
    const fs::path metalSrc = resolveSourceImage(aiMat, aiTextureType_METALNESS,
                                                 scene, sourceDir, subDir, caches);
    if (!roughSrc.empty() && roughSrc == metalSrc)
    {
        auto [roughImg, metalImg] = splitPackedORM(roughSrc, subDir);
        desc.roughnessTexPath = writeBtexFor(roughImg, subDir, baseDir,
                                             TextureColorSpace::Linear, caches);
        desc.metallicTexPath  = writeBtexFor(metalImg, subDir, baseDir,
                                             TextureColorSpace::Linear, caches);
    }
    else
    {
        desc.roughnessTexPath = writeBtexFor(roughSrc, subDir, baseDir,
                                             TextureColorSpace::Linear, caches);
        desc.metallicTexPath  = writeBtexFor(metalSrc, subDir, baseDir,
                                             TextureColorSpace::Linear, caches);
    }

    fs::path matPath = subDir / (name + ".bmat");
    writeBmat(desc, matPath.string());

    std::string rel        = fs::relative(matPath, baseDir).generic_string();
    caches.matCache[matIdx] = rel;
    return rel;
}

static void processNode(const aiNode* node, const aiScene* scene, const fs::path& subDir,
                        std::string_view baseDir, const fs::path& sourceDir,
                        std::vector<EntityDesc>& entities,
                        int parentIndex, DecomposeResult& result, ImportCaches& caches)
{
    aiVector3D pos, scale;
    aiQuaternion rot;
    node->mTransformation.Decompose(scale, rot, pos);

    const bool identityTransform = pos.x == 0.f && pos.y == 0.f && pos.z == 0.f && rot.x == 0.f &&
                                   rot.y == 0.f && rot.z == 0.f && rot.w == 1.f && scale.x == 1.f &&
                                   scale.y == 1.f && scale.z == 1.f;

    // Skip empty wrapper nodes (no mesh, identity transform)
    if (node->mNumMeshes == 0 && identityTransform)
    {
        for (unsigned i = 0; i < node->mNumChildren; ++i)
            processNode(node->mChildren[i], scene, subDir, baseDir, sourceDir, entities,
                        parentIndex, result, caches);
        return;
    }

    EntityDesc desc;
    desc.name = node->mName.C_Str();
    desc.parentIndex = parentIndex;
    desc.components.push_back(Transform_C::fromPosRotScale(
        {pos.x, pos.y, pos.z}, {rot.w, rot.x, rot.y, rot.z}, {scale.x, scale.y, scale.z}));

    if (node->mNumMeshes > 0)
    {
        BmeshData merged = mergeNodeMeshes(node, scene);
        std::string name = desc.name.empty() ? "mesh" : desc.name;
        fs::path bmeshPath = subDir / (name + ".bmesh");
        if (writeBmesh(merged, bmeshPath.string()))
            pushRel(result.bmeshPaths, bmeshPath, baseDir);

        desc.components.push_back(MeshDesc{relToBase(bmeshPath, baseDir)});

        MaterialsDesc matDesc;
        const uint32_t matCount = std::min(node->mNumMeshes, 8u);
        for (uint32_t i = 0; i < matCount; ++i)
        {
            const aiMesh* m = scene->mMeshes[node->mMeshes[i]];
            matDesc.paths[i] =
                getOrWriteMaterial(m->mMaterialIndex, scene, subDir, baseDir, sourceDir,
                                   caches);
        }
        desc.components.push_back(matDesc);
    }

    int myIndex = static_cast<int>(entities.size());
    entities.push_back(std::move(desc));

    for (unsigned i = 0; i < node->mNumChildren; ++i)
        processNode(node->mChildren[i], scene, subDir, baseDir, sourceDir, entities, myIndex,
                    result, caches);
}

static void resetRootTransform(std::vector<EntityDesc>& entities)
{
    for (auto& desc : entities)
    {
        if (desc.parentIndex != -1)
            continue;
        for (auto& comp : desc.components)
            if (auto* tc = std::get_if<Transform_C>(&comp))
            {
                *tc = Transform_C::fromPosRotScale({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f, 0.f},
                                                   {1.f, 1.f, 1.f});
                break;
            }
    }
}

static std::vector<EntityDesc> extractSubtree(const std::vector<EntityDesc>& all, int rootIdx)
{
    // Collect indices via BFS
    std::vector<int> indices;
    std::vector<int> queue = {rootIdx};
    while (!queue.empty())
    {
        int cur = queue.back();
        queue.pop_back();
        indices.push_back(cur);
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].parentIndex == cur)
                queue.push_back(static_cast<int>(i));
    }

    // Map old index → new index
    std::unordered_map<int, int> remap;
    for (size_t i = 0; i < indices.size(); ++i)
        remap[indices[i]] = static_cast<int>(i);

    std::vector<EntityDesc> sub;
    sub.reserve(indices.size());
    for (int oldIdx : indices)
    {
        EntityDesc d = all[static_cast<size_t>(oldIdx)];
        d.parentIndex = (oldIdx == rootIdx) ? -1 : remap.at(d.parentIndex);
        sub.push_back(std::move(d));
    }
    return sub;
}

DecomposeResult decomposeSourceFile(std::string_view sourcePath, std::string_view outputDir,
                                    std::string_view baseDir)
{
    DecomposeResult result;

    Assimp::Importer importer;
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 45.0f);

    const unsigned flags = aiProcess_JoinIdenticalVertices | aiProcess_Triangulate |
                           aiProcess_DropNormals | aiProcess_GenSmoothNormals;

    const aiScene* scene = importer.ReadFile(std::string(sourcePath), flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
    {
        std::cerr << "[MeshDecomposer] Failed to load: " << sourcePath << "\n";
        return result;
    }

    fs::path outDir(outputDir);
    std::string baseName = fs::path(sourcePath).stem().string();
    fs::path subDir = outDir / baseName;

    if (scene->mNumMeshes > 0)
        fs::create_directories(subDir);

    // Build entity hierarchy — bmesh, bmat and btex files are written per-node inside processNode
    fs::path sourceDir = fs::path(sourcePath).parent_path();
    std::vector<EntityDesc> entities;
    ImportCaches caches;
    processNode(scene->mRootNode, scene, subDir, baseDir, sourceDir, entities, -1, result,
                caches);
    for (auto& [idx, rel] : caches.matCache)
        result.bmatPaths.push_back(rel);
    for (auto& [texKey, rel] : caches.texCache)
        result.texturePaths.push_back(rel);
    // Embedded images and split ORM maps are written too, and an image already in
    // the project can be loaded under its own path.
    for (auto& [ref, img] : caches.imgCache)
        if (!img.empty())
            pushRel(result.texturePaths, fs::path(img), baseDir);

    // Collect root indices
    std::vector<int> roots;
    for (size_t i = 0; i < entities.size(); ++i)
        if (entities[i].parentIndex == -1)
            roots.push_back(static_cast<int>(i));

    if (roots.size() == 1)
    {
        // Single object — one btpl only, root at identity
        resetRootTransform(entities);
        fs::path btplPath = outDir / (baseName + ".btpl");
        EntitySerializer::save(entities, btplPath.string());
        pushRel(result.btplPaths, btplPath, baseDir);
    }
    else
    {
        // Per-object btpls in a subfolder (subDir already created with bmesh files)
        fs::create_directories(subDir);
        for (int rootIdx : roots)
        {
            auto sub = extractSubtree(entities, rootIdx);
            resetRootTransform(sub);
            fs::path btplPath = subDir / (entities[static_cast<size_t>(rootIdx)].name + ".btpl");
            EntitySerializer::save(sub, btplPath.string());
            pushRel(result.btplPaths, btplPath, baseDir);
        }
        // Global btpl
        fs::path globalPath = outDir / (baseName + ".btpl");
        EntitySerializer::save(entities, globalPath.string());
        pushRel(result.btplPaths, globalPath, baseDir);
    }

    result.ok = true;

    return result;
}

#pragma clang diagnostic pop

}  // namespace batap
