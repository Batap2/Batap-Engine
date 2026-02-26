#pragma once

#include <string>
#include <tuple>
#include <utility>
#include "AssetHandle.h"
#include "AssetSlotMap.h"


namespace batap
{

struct ResourceManager;
struct Mesh;
struct Texture;

template <typename T>
struct AssetSlotMap;

struct AssetManager
{
    explicit AssetManager(ResourceManager* rm);
    ~AssetManager();

    template <typename T, typename... Args>
    std::pair<AssetHandle<T>, bool> emplace(std::string name, std::string path, Args&&... args)
    {
        return std::get<AssetSlotMap<T>*>(maps_)->emplace(std::move(name), std::move(path),
                                                          std::forward<Args>(args)...);
    }

    template <typename T>
    T* get(AssetHandle<T> key)
    {
        return std::get<AssetSlotMap<T>*>(maps_)->get(key);
    }

    template <typename T>
    T* get(const std::string& path)
    {
        return std::get<AssetSlotMap<T>*>(maps_)->get(path);
    }

    ResourceManager* resourceManager_ = nullptr;

   private:
    std::tuple<AssetSlotMap<Mesh>*, AssetSlotMap<Texture>*> maps_{};
};

}  // namespace batap
