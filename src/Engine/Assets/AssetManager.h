#pragma once

#include <optional>
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

    template <typename T>
    const std::string* getPath(AssetHandle<T> key)
    {
        auto* asset = std::get<AssetSlotMap<T>*>(maps_)->getAsset(key);
        return asset ? &asset->path_ : nullptr;
    }

    template <typename T>
    std::optional<AssetHandle<T>> getHandle(const std::string& path)
    {
        auto& map = *std::get<AssetSlotMap<T>*>(maps_);
        auto it = map.pathToKey_.find(path);
        if (it == map.pathToKey_.end())
            return std::nullopt;
        return it->second;
    }

    using ForEachAssetMetaFn =
        std::function<void(AssetHandleAny handle, std::string_view name, std::string_view path)>;

    void forEachAssetOfType(AssetType type, const ForEachAssetMetaFn& fn) const;

    // Must be called before any loadAsset call. Asserts if dir is empty.
    void               setBaseDir(std::string dir);
    const std::string& baseDir() const { return baseDir_; }

    ResourceManager* resourceManager_ = nullptr;

   private:
    std::string baseDir_;
    std::tuple<AssetSlotMap<Mesh>*, AssetSlotMap<Texture>*> maps_{};
};

}  // namespace batap
