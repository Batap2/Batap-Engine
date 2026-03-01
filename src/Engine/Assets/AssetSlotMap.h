#pragma once

#include <emhash/hash_table8.hpp>
#include "AssetHandle.h"
#include "Utils/SlotMap.h"

#include <string>
#include <utility>
#include <vector>

namespace batap
{
template <typename T>
struct AssetSlotMap
{
    struct Asset
    {
        T value_;
        std::string name_;
        std::string path_;  // could be empty
    };

    using Key = AssetHandle<T>;
    using InternalKey = typename SlotMap<Asset>::Key;

    static InternalKey toInternal(Key h) { return InternalKey{h.index, h.generation}; }
    static Key toPublic(InternalKey k) { return Key{k.index, k.generation}; }

    SlotMap<Asset> slotMap_;
    emhash8::HashMap<std::string, std::vector<Key>> nameToKeys_;
    emhash8::HashMap<std::string, Key> pathToKey_;

    T* get(Key key)
    {
        Asset* a = slotMap_.get(toInternal(key));
        return a ? &a->value_ : nullptr;
    }

    const T* get(Key key) const
    {
        const Asset* a = slotMap_.get(toInternal(key));
        return a ? &a->value_ : nullptr;
    }

    Asset* getAsset(Key key) { return slotMap_.get(toInternal(key)); }
    const Asset* getAsset(Key key) const { return slotMap_.get(toInternal(key)); }

    template <typename... Args>
    std::pair<Key, bool> emplace(std::string name, std::string path, Args&&... args)
    {
        if (!path.empty())
        {
            if (pathToKey_.find(path) != pathToKey_.end())
                return {pathToKey_[path], false};
        }

        InternalKey ik = slotMap_.emplace(
            Asset{T(std::forward<Args>(args)...), std::move(name), std::move(path)});

        Asset* a = slotMap_.get(ik);
        if (!a)
            return {Key{}, false};

        Key k = toPublic(ik);

        if (!a->name_.empty())
            nameToKeys_[a->name_].push_back(k);

        if (!a->path_.empty())
            pathToKey_.emplace(a->path_, k);

        return {k, true};
    }

    bool erase(Key key)
    {
        InternalKey ik = toInternal(key);

        Asset* a = slotMap_.get(ik);
        if (!a)
            return false;

        if (!a->name_.empty())
        {
            if (auto it = nameToKeys_.find(a->name_); it != nameToKeys_.end())
            {
                std::erase(it->second, key);
                if (it->second.empty())
                    nameToKeys_.erase(it);
            }
        }

        if (!a->path_.empty())
        {
            pathToKey_.erase(a->path_);
        }

        return slotMap_.erase(ik);
    }

    bool eraseByPath(const std::string& path)
    {
        if (path.empty())
            return false;

        auto it = pathToKey_.find(path);
        if (it == pathToKey_.end())
            return false;

        return erase(it->second);
    }

    const std::vector<Key>* keysByName(const std::string& name) const
    {
        auto it = nameToKeys_.find(name);
        return (it == nameToKeys_.end()) ? nullptr : &it->second;
    }

    T* get(const std::string& path)
    {
        if (path.empty())
            return nullptr;
        auto it = pathToKey_.find(path);
        return (it == pathToKey_.end()) ? nullptr : get(it->second);
    }

    const T* get(const std::string& path) const
    {
        if (path.empty())
            return nullptr;
        auto it = pathToKey_.find(path);
        return (it == pathToKey_.end()) ? nullptr : get(it->second);
    }
};
}  // namespace batap
