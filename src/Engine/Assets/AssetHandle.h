#pragma once
#include <cstdint>
#include <variant>

namespace batap {

struct Mesh;
struct Texture;

template<class T>
struct AssetHandle
{
    uint32_t index = 0;
    uint32_t generation = 0;

    friend bool operator==(AssetHandle a, AssetHandle b) {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(AssetHandle a, AssetHandle b) { return !(a == b); }

    static constexpr AssetHandle null() { return {}; }
    explicit operator bool() const { return generation != 0; }
};

using MeshHandle = AssetHandle<Mesh>;
using TextureHandle = AssetHandle<Texture>;

using AssetHandleAny = std::variant<MeshHandle, TextureHandle>;
} // namespace batap
