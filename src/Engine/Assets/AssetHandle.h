#pragma once
#include <cstdint>
#include <variant>

namespace batap
{

struct Mesh;
struct Texture;
struct Material;

template <class T>
struct AssetHandle
{
    uint32_t index = 0;
    uint32_t generation = 0;

    friend bool operator==(AssetHandle a, AssetHandle b)
    {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(AssetHandle a, AssetHandle b) { return !(a == b); }

    static constexpr AssetHandle null() { return {}; }
    explicit operator bool() const { return generation != 0; }
};

using MeshHandle     = AssetHandle<Mesh>;
using TextureHandle  = AssetHandle<Texture>;
using MaterialHandle = AssetHandle<Material>;

using AssetHandleAny = std::variant<MeshHandle, TextureHandle, MaterialHandle>;

enum class AssetType : uint8_t
{
    Mesh,
    Texture,
    Material,
};
}  // namespace batap
