#pragma once

#include "Handles.h"

#include "VariantUtils.h"
#include "emhash/hash_table8.hpp"

#include <memory>
#include <optional>
#include <variant>

namespace batap
{

struct ResourceManager;
struct Mesh;
struct Texture;

struct AssetManager
{
    AssetManager(ResourceManager* rm);
    ~AssetManager();

    emhash8::HashMap<AssetHandle, Mesh> _meshes;
    emhash8::HashMap<AssetHandle, Texture> _textures;

    template <typename T>
    struct EmplaceResult
    {
        T* _obj = nullptr;
        AssetHandle _handle;
        bool _alreadyExist = false;
    };

    EmplaceResult<Mesh> emplaceMesh(std::optional<std::string> name = std::nullopt);
    EmplaceResult<Texture> emplaceTexture(std::optional<std::string> name = std::nullopt);

    template <typename T>
    EmplaceResult<T> emplace(std::optional<std::string> name = std::nullopt)
    {
        AssetHandle h;
        bool alreadyExist = false;

        emhash8::HashMap<AssetHandle, T>* map = nullptr;

        AssetHandle::ObjectType objType;
        if constexpr (std::is_same_v<T, Texture>){
            objType = AssetHandle::ObjectType::Texture;
            map = _textures;
        } else if constexpr (std::is_same_v<T, Mesh>){
            objType = AssetHandle::ObjectType::Mesh;
            map = _meshes;
        }

        if (name)
        {
            h = AssetHandle(objType, name.value());
        }
        else
        {
            h = AssetHandle(AssetHandle::ObjectType::Mesh);
        }

        auto emp = map->try_emplace();

        if(!emp->second)
        {
            alreadyExist = true;
        }

        return {map->at(h), h, alreadyExist};
    }

    ResourceManager* _resourceManager;
};
}  // namespace batap
