// un jour remplacer par :
// https://github.com/spnda/fastgltf?tab=readme-ov-file
// https://github.com/ufbx/ufbx
// mesh optimizer

#include "MeshDecomposer.h"

#include "Serialization/BmeshSerializer.h"
#include "Serialization/EntityDesc.h"
#include "Serialization/EntitySerializer.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace batap
{

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"

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
            data.uvs.push_back({mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y});
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

static void processNode(const aiNode* node, const aiScene* scene, const fs::path& subDir,
                        std::string_view baseDir, std::vector<EntityDesc>& entities,
                        int parentIndex, DecomposeResult& result)
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
            processNode(node->mChildren[i], scene, subDir, baseDir, entities, parentIndex, result);
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
            result.bmeshPaths.push_back(bmeshPath.string());

        desc.kind = "mesh";
        desc.components.push_back(MeshDesc{fs::relative(bmeshPath, baseDir).generic_string()});
    }

    int myIndex = static_cast<int>(entities.size());
    entities.push_back(std::move(desc));

    for (unsigned i = 0; i < node->mNumChildren; ++i)
        processNode(node->mChildren[i], scene, subDir, baseDir, entities, myIndex, result);
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

    // Build entity hierarchy — bmesh files are written per-node inside processNode
    std::vector<EntityDesc> entities;
    processNode(scene->mRootNode, scene, subDir, baseDir, entities, -1, result);

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
        result.btplPaths.push_back(btplPath.string());
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
            result.btplPaths.push_back(btplPath.string());
        }
        // Global btpl
        fs::path globalPath = outDir / (baseName + ".btpl");
        EntitySerializer::save(entities, globalPath.string());
        result.btplPaths.push_back(globalPath.string());
    }

    result.ok = true;

    return result;
}

#pragma clang diagnostic pop

}  // namespace batap
