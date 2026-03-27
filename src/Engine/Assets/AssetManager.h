#pragma once

#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include "AssetGPUArena.h"
#include "AssetHandle.h"
#include "AssetSlotMap.h"

namespace batap
{

struct ResourceManager;
struct Mesh;
struct Texture;
struct Material;

template <typename T>
struct AssetSlotMap;

template <typename T>
struct AssetGPUArena;

// Associate each GPU-arena asset type with its tuple slot
template <typename T>
struct IsGPUArenaAsset : std::false_type
{};
template <>
struct IsGPUArenaAsset<Material> : std::true_type
{};

struct AssetManager
{
    explicit AssetManager(ResourceManager* rm);
    ~AssetManager();

    template <typename T, typename... Args>
    std::pair<AssetHandle<T>, bool> emplace(std::string name, std::string path, Args&&... args)
    {
        if constexpr (IsGPUArenaAsset<T>::value)
            return getGPUArena<T>()->insert(path, std::move(name), T{std::forward<Args>(args)...});
        else
            return getSlotMap<T>()->emplace(std::move(name), std::move(path),
                                            std::forward<Args>(args)...);
    }

    template <typename T>
    auto* get(AssetHandle<T> key)
    {
        if constexpr (IsGPUArenaAsset<T>::value)
            return getGPUArena<T>()->get(key);
        else
            return getSlotMap<T>()->get(key);
    }

    template <typename T>
    auto* get(const std::string& path)
    {
        if constexpr (IsGPUArenaAsset<T>::value)
            return getGPUArena<T>()->get(path);
        else
            return getSlotMap<T>()->get(path);
    }

    template <typename T>
    const std::string* getPath(AssetHandle<T> key)
    {
        if constexpr (IsGPUArenaAsset<T>::value)
            return getGPUArena<T>()->getPath(key);
        else
        {
            auto* asset = getSlotMap<T>()->getAsset(key);
            return asset ? &asset->path_ : nullptr;
        }
    }

    template <typename T>
    std::optional<AssetHandle<T>> getHandle(const std::string& path)
    {
        if constexpr (IsGPUArenaAsset<T>::value)
            return getGPUArena<T>()->getKey(path);
        else
        {
            auto& map = *getSlotMap<T>();
            auto  it  = map.pathToKey_.find(path);
            if (it == map.pathToKey_.end())
                return std::nullopt;
            return it->second;
        }
    }

    template <typename T>
    bool update(AssetHandle<T> key, T value)
    {
        if constexpr (IsGPUArenaAsset<T>::value)
            return getGPUArena<T>()->update(key, std::move(value));
        else
            return false;
    }

    template <typename T>
    AssetSlotMap<T>* getSlotMap()
    {
        return std::get<AssetSlotMap<T>*>(maps_);
    }

    template <typename T>
    AssetGPUArena<T>* getGPUArena()
    {
        return std::get<AssetGPUArena<T>*>(gpuArenas_);
    }

    template <typename T>
    const AssetGPUArena<T>* getGPUArena() const
    {
        return std::get<AssetGPUArena<T>*>(gpuArenas_);
    }

    // Saves all loaded GPU-arena assets (materials, ...) back to disk.
    void saveAllAssets() const;

    // Must be called before any loadAsset call. Asserts if dir is empty.
    void setBaseDir(std::string dir);
    const std::string& baseDir() const { return baseDir_; }

    ResourceManager* resourceManager_ = nullptr;

   private:
    std::string baseDir_;
    std::tuple<AssetSlotMap<Mesh>*, AssetSlotMap<Texture>*> maps_{};
    std::tuple<AssetGPUArena<Material>*> gpuArenas_{};
};

}  // namespace batap
