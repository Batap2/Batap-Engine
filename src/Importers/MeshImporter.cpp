#include "MeshImporter.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "EigenTypes.h"
#include "Handles.h"
#include "Renderer/ResourceFormatWrapper.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/includeDX12.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

// #include "stb_image.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace rayvox
{

std::vector<AssetHandle> importMeshFromFile(std::string_view path, AssetManager& assetManager)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    auto& rm = assetManager._resourceManager;
    Assimp::Importer importer;

    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 45.0f);

    std::vector<AssetHandle> result;
    unsigned int flags;

    // if (args.flatFaces)
    // {
    //     flags = aiProcess_Triangulate | aiProcess_DropNormals | aiProcess_GenNormals;
    // }
    // else
    {
        flags = aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_DropNormals |
                aiProcess_GenSmoothNormals;
    }

    const aiScene* aiScene_mesh = importer.ReadFile(path.data(), flags);

    if (aiScene_mesh == nullptr)
    {
        std::cout << "Mesh file not found or cannot be imported" << std::endl;
        return {};
    }

    auto meshNumber = aiScene_mesh->mNumMeshes;
    // auto textureNumber = aiScene_mesh->mNumTextures;

    for (size_t i = 0; i < meshNumber; ++i)
    {
        aiMesh* aiMesh_mesh = aiScene_mesh->mMeshes[i];

        unsigned int sizeV = aiMesh_mesh->mNumVertices;
        unsigned int sizeT = aiMesh_mesh->mNumFaces;

        std::vector<uint32_t> indices;
        std::vector<v3f> vertices, normals;
        std::vector<v4f> colors;
        std::vector<v2f> uvs;

        for (unsigned int j = 0; j < sizeV; ++j)
        {
            vertices.push_back({aiMesh_mesh->mVertices[j].x, aiMesh_mesh->mVertices[j].y,
                                aiMesh_mesh->mVertices[j].z});
            normals.push_back({aiMesh_mesh->mNormals[j].x, aiMesh_mesh->mNormals[j].y,
                               aiMesh_mesh->mNormals[j].z});
            v2f uv = {0, 0};
            v4f c;

            if (aiMesh_mesh->HasVertexColors(0))
            {
                c = {aiMesh_mesh->mColors[0][j].r, aiMesh_mesh->mColors[0][j].g,
                     aiMesh_mesh->mColors[0][j].b, aiMesh_mesh->mColors[0][j].a};
            }
            else
            {
                c = {1, 1, 1, 1};
            }

            colors.push_back(c);

            if (aiMesh_mesh->HasTextureCoords(0))
            {
                uv = {aiMesh_mesh->mTextureCoords[0][j].x, aiMesh_mesh->mTextureCoords[0][j].y};
            }
            uvs.push_back(uv);
        }

        for (unsigned int j = 0; j < sizeT; ++j)
        {
            indices.push_back(aiMesh_mesh->mFaces[j].mIndices[0]);
            indices.push_back(aiMesh_mesh->mFaces[j].mIndices[1]);
            indices.push_back(aiMesh_mesh->mFaces[j].mIndices[2]);
        }

        std::string sceneName = aiScene_mesh->mName.C_Str();
        std::string meshName = aiScene_mesh->mName.C_Str();
        auto ent = assetManager.emplaceMesh(sceneName + ":" + meshName);

        if (ent._alreadyExist)
        {
            auto* newMesh = ent._mesh;
            {
                auto bufSize = sizeof(uint32_t) * indices.size();
                auto resourceGuid =
                    rm->createBufferStaticResource(bufSize, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   D3D12_HEAP_TYPE_DEFAULT, "meshResource_i");

                newMesh->_indexBuffer = rm->createStaticIBV(resourceGuid, ResourceFormat::R32_UINT,
                                                            "meshView_i", 0, bufSize);

                auto dataSpan = rm->requestUploadOwned(resourceGuid, bufSize, 0);
                std::memcpy(dataSpan.data(), indices.data(), bufSize);
            }
            {
                auto bufSize = sizeof(v3f) * vertices.size();
                auto resourceGuid =
                    rm->createBufferStaticResource(bufSize, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   D3D12_HEAP_TYPE_DEFAULT, "meshResource_v");

                newMesh->_vertexBuffer =
                    rm->createStaticVBV(resourceGuid, sizeof(v3f), "meshView_v", 0, bufSize);

                auto dataSpan =
                    rm->requestUploadOwned(resourceGuid, bufSize, 0);
                std::memcpy(dataSpan.data(), vertices.data(), bufSize);
            }
        }

        result.push_back(ent._handle);
    }

    return result;
#pragma clang diagnostic pop
}

// --------------- TEXTURE ---------------- //

// if (aiMesh->HasTextureCoords(0)) {
//
//     // Access material
//     aiMaterial* material = aiScene->mMaterials[aiMesh->mMaterialIndex];
//
//     // Access the texture count
//     unsigned int textureCount = material->GetTextureCount(aiTextureType_DIFFUSE);
//
//     // Loop through all textures of the mesh
//     for (unsigned int j = 0; j < textureCount; ++j) {
//         aiString texturePath;
//         material->GetTexture(aiTextureType_DIFFUSE, j, &texturePath);
//
//         const aiTexture* aiTex = aiScene->GetEmbeddedTexture(texturePath.C_Str());
//
//         auto& texture = area->Entities.emplace<Texture>(newEntt);
//
//         // Si la texture est sous un format d'image compr�ss�
//         // appelle stb_image.h pour d�compr�sser
//         if (aiTex->mHeight == 0) {
//
//             int width, height, channels;
//
//             unsigned char* imageData = stbi_load_from_memory(
//                 reinterpret_cast<unsigned char*>(aiTex->pcData),
//                 aiTex->mWidth,
//                 &width,
//                 &height,
//                 &channels,
//                 STBI_rgb
//             );
//
//             if (!imageData) {
//                 std::cerr << "Failed to load embedded compressed texture\n";
//             }
//
//             texture.Size = { width, height };
//
//             for (int y = 0; y < height; ++y)
//             {
//                 for (int x = 0; x < width; ++x)
//                 {
//                     int pos = (x * channels) + (y * channels) * width;
//
//                     texture.data.push_back(imageData[pos]);
//                     texture.data.push_back(imageData[pos + 1]);
//                     texture.data.push_back(imageData[pos + 2]);
//                 }
//             }
//
//             stbi_image_free(imageData);
//         }
//         else
//         {
//             texture.height = aiTex->mHeight;
//             texture.width = aiTex->mWidth;
//
//             for (int y = 0; y < aiTex->mHeight; ++y)
//             {
//                 for (int x = 0; x < aiTex->mWidth; ++x)
//                 {
//                     int pos = x + y * aiTex->mWidth;
//                     texture.data.push_back(aiTex->pcData[pos].r);
//                     texture.data.push_back(aiTex->pcData[pos].g);
//                     texture.data.push_back(aiTex->pcData[pos].b);
//                 }
//             }
//         }
//
//         newMesh->material.diffuse_texture = texture;
//         newMesh->material.useTexture = 1;
//     }
}  // namespace rayvox
