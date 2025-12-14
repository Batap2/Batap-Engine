#include "MeshImporter.h"

//#include "assimp/Importer.hpp"
// #include "assimp/postprocess.h"
// #include "assimp/scene.h"
// #include "stb_image.h"

#include <iostream>
#include <vector>
#include "Handles.h"

namespace rayvox
{

AssetHandle importMeshFromFile(std::string_view path) {
    return AssetHandle();
}
// std::vector<EntityHandle> CreateEntitiesFromFile(MeshImporter_Params& args)
// {
//     args.area->BeginUpdate();
//     kigs_defer
//     {
//         args.area->EndUpdate();
//     };

//     std::vector<EntityHandle> result;
//     std::vector<MeshUtilsCollider> colliders;
//     Assimp::Importer importer;
//     importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 45.0f);

//     unsigned int flags;

//     if (args.flatFaces)
//     {
//         flags = aiProcess_Triangulate | aiProcess_DropNormals | aiProcess_GenNormals;
//     }
//     else
//     {
//         flags = aiProcess_JoinIdenticalVertices | aiProcess_Triangulate | aiProcess_DropNormals |
//                 aiProcess_GenSmoothNormals;
//     }

//     if (args.colliderFile != "")
//     {
//         const aiScene* aiScene_collider = importer.ReadFile(args.colliderFile, flags);

//         if (nullptr == aiScene_collider)
//         {
//             std::cout << "Collider file not found or cannot be imported" << std::endl;
//             return {};
//         }

//         int colliderNumber = aiScene_collider->mNumMeshes;

//         for (int i = 0; i < colliderNumber; ++i)
//         {
//             aiMesh* aiMesh_collider = aiScene_collider->mMeshes[i];

//             u32 sizeV_c = aiMesh_collider->mNumVertices;
//             u32 sizeT_c = aiMesh_collider->mNumFaces;

//             std::vector<u32> indices_c;
//             std::vector<v3f> vertices_c;

//             for (unsigned int j = 0; j < sizeV_c; ++j)
//             {
//                 vertices_c.push_back({aiMesh_collider->mVertices[j].x,
//                                       aiMesh_collider->mVertices[j].y,
//                                       aiMesh_collider->mVertices[j].z});
//             }

//             for (unsigned int j = 0; j < sizeT_c; ++j)
//             {
//                 indices_c.push_back(aiMesh_collider->mFaces[j].mIndices[0]);
//                 indices_c.push_back(aiMesh_collider->mFaces[j].mIndices[1]);
//                 indices_c.push_back(aiMesh_collider->mFaces[j].mIndices[2]);
//             }

//             colliders.push_back({indices_c, vertices_c});
//         }
//     }

//     const aiScene* aiScene_mesh = importer.ReadFile(args.pFile, flags);

//     if (nullptr == aiScene_mesh)
//     {
//         std::cout << "Mesh file not found or cannot be imported" << std::endl;
//         return {};
//     }

//     int meshNumber = aiScene_mesh->mNumMeshes;
//     int textureNumber = aiScene_mesh->mNumTextures;

//     for (int i = 0; i < meshNumber; ++i)
//     {
//         aiMesh* aiMesh_mesh = aiScene_mesh->mMeshes[i];

//         unsigned int sizeV = aiMesh_mesh->mNumVertices;
//         unsigned int sizeT = aiMesh_mesh->mNumFaces;

//         std::vector<u32> indices;
//         std::vector<v3f> vertices, normals;
//         std::vector<v4f> colors;
//         std::vector<v2f> uvs;

//         for (unsigned int j = 0; j < sizeV; ++j)
//         {
//             vertices.push_back({aiMesh_mesh->mVertices[j].x, aiMesh_mesh->mVertices[j].y,
//                                 aiMesh_mesh->mVertices[j].z});
//             normals.push_back({aiMesh_mesh->mNormals[j].x, aiMesh_mesh->mNormals[j].y,
//                                aiMesh_mesh->mNormals[j].z});
//             v2f uv = {0, 0};
//             v4f c = args.color;

//             if (args.color.x() == -1)
//             {
//                 if (aiMesh_mesh->HasVertexColors(0))
//                 {
//                     c = {aiMesh_mesh->mColors[0][j].r, aiMesh_mesh->mColors[0][j].g,
//                          aiMesh_mesh->mColors[0][j].b, aiMesh_mesh->mColors[0][j].a};
//                 }
//                 else
//                 {
//                     c = {1, 1, 1, 1};
//                 }
//             }
//             colors.push_back(c);

//             if (aiMesh_mesh->HasTextureCoords(0))
//             {
//                 uv = {aiMesh_mesh->mTextureCoords[0][j].x, aiMesh_mesh->mTextureCoords[0][j].y};
//             }
//             uvs.push_back(uv);
//         }

//         for (unsigned int j = 0; j < sizeT; ++j)
//         {
//             indices.push_back(aiMesh_mesh->mFaces[j].mIndices[0]);
//             indices.push_back(aiMesh_mesh->mFaces[j].mIndices[1]);
//             indices.push_back(aiMesh_mesh->mFaces[j].mIndices[2]);
//         }

//         MeshUtils_Param mp;
//         mp.ctx = args.ctx;
//         mp.area = args.area;
//         mp.indices = &indices;
//         mp.vertices = &vertices;
//         mp.normals = &normals;
//         mp.colors = &colors;
//         mp.uvs = &uvs;
//         mp.hasCollider = args.hasCollider;

//         if (args.hasCollider && args.meshColliderAssociationTable.size() > 0)
//         {
//             u32 colliderIndex;
//             if (args.meshColliderAssociationTable.find(i) !=
//                 args.meshColliderAssociationTable.end())
//             {
//                 colliderIndex = args.meshColliderAssociationTable[i];

//                 if (colliderIndex == -1)
//                 {
//                     mp.hasCollider = false;
//                 }
//                 else
//                 {
//                     mp.custom_collider_indices = &colliders[colliderIndex].indices;
//                     mp.custom_collider_vertices = &colliders[colliderIndex].vertices;
//                 }
//             }
//         }

//         auto ent = createMeshEntity(mp);
//         result.push_back(ent);
//     }
//     return result;
// }
// }  // namespace rayvox

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