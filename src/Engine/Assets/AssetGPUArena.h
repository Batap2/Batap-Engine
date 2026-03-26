#pragma once

#include "AssetHandle.h"
#include "Utils/GPUArena.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace batap
{

template <typename T>
struct AssetGPUArena
{
    using Key = AssetHandle<T>;

    AssetGPUArena() = default;

    static AssetGPUArena create(ResourceManager& rm, size_t initCapacity, std::string name)
    {
        AssetGPUArena a;
        a.arena_ = GPUArena<T, Key>::create(rm, initCapacity, std::move(name));
        return a;
    }

    std::pair<Key, bool> insert(const std::string& path, std::string name, T value)
    {
        if (auto it = pathToKey_.find(path); it != pathToKey_.end())
            return {it->second, false};
        Key k = arena_.insert(std::move(value));
        pathToKey_[path] = k;
        auto pk = std::make_pair(k.index, k.generation);
        keyToPath_[pk] = path;
        keyToName_[pk] = std::move(name);
        return {k, true};
    }

    bool erase(Key k)
    {
        auto pk = std::make_pair(k.index, k.generation);
        if (auto it = keyToPath_.find(pk); it != keyToPath_.end())
        {
            pathToKey_.erase(it->second);
            keyToPath_.erase(it);
        }
        keyToName_.erase(pk);
        return arena_.erase(k);
    }

    bool          update(Key k, T v)       { return arena_.update(k, std::move(v)); }
    const T*      get(Key k) const         { return arena_.get(k); }
    bool          contains(Key k) const    { return arena_.contains(k); }
    GPUViewHandle srvHandle() const        { return arena_.srvHandle(); }

    std::optional<Key> getKey(const std::string& path) const
    {
        auto it = pathToKey_.find(path);
        return it != pathToKey_.end() ? std::optional{it->second} : std::nullopt;
    }

    const T* get(const std::string& path) const
    {
        auto it = pathToKey_.find(path);
        return it != pathToKey_.end() ? arena_.get(it->second) : nullptr;
    }

    const std::string* getPath(Key k) const
    {
        auto it = keyToPath_.find({k.index, k.generation});
        return it != keyToPath_.end() ? &it->second : nullptr;
    }

    const std::string* getName(Key k) const
    {
        auto it = keyToName_.find({k.index, k.generation});
        return it != keyToName_.end() ? &it->second : nullptr;
    }

    template <typename Fn>
    void forEach(Fn&& fn) const
    {
        for (const auto& [path, key] : pathToKey_)
        {
            const std::string* name = getName(key);
            fn(key, name ? *name : std::string{}, path);
        }
    }

   private:
    struct PairHash
    {
        size_t operator()(std::pair<uint32_t, uint32_t> p) const noexcept
        {
            return std::hash<uint64_t>{}(static_cast<uint64_t>(p.first) << 32 | p.second);
        }
    };

    using PairMap = std::unordered_map<std::pair<uint32_t, uint32_t>, std::string, PairHash>;

    GPUArena<T, Key>                         arena_;
    std::unordered_map<std::string, Key>     pathToKey_;
    PairMap                                  keyToPath_;
    PairMap                                  keyToName_;
};

}  // namespace batap
