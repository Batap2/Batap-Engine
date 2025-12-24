#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <variant>

#include "magic_enum/magic_enum.hpp"

namespace rayvox
{

template <typename TypeEnum>
    requires std::is_enum_v<TypeEnum>
struct Handle
{
    using ObjectType = TypeEnum;
    Handle() = default;

    explicit Handle(ObjectType type) : _type(type), _guid(random64()) {}

    Handle(ObjectType type, std::string_view name)
        : _type(type), _guid(hash64(name) ^ (uint64_t(type) * 0x9e3779b97f4a7c15ull))
    {}

    bool operator==(const Handle& other) const
    {
        return _type == other._type && _guid == other._guid;
    }

    std::string toString() const
    {
        std::string s(magic_enum::enum_name(_type));
        s += ":";
        s += std::to_string(_guid);
        return s;
    }

    ObjectType _type{};
    uint64_t _guid = 0;

   private:
    static uint64_t random64()
    {
        static thread_local std::mt19937_64 rng{std::random_device{}()};
        return rng();
    }

    // FNV-1a
    static constexpr uint64_t hash64(std::string_view s)
    {
        uint64_t h = 14695981039346656037ull;
        for (const char c : s)
        {
            h ^= uint64_t(c);
            h *= 1099511628211ull;
        }
        return h;
    }
};

enum class GPUResourceType : uint8_t
{
    Unknown,
    StaticResource,
    FrameResource
};
using GPUResourceHandle = Handle<GPUResourceType>;

enum class GPUViewType : uint8_t
{
    Unknown,
    StaticView,
    FrameView
};
using GPUViewHandle = Handle<GPUViewType>;

enum class AssetType : uint8_t
{
    Unknown,
    Mesh,
    Texture,
    Material,
    Shader
};
using AssetHandle = Handle<AssetType>;

enum class GPUMeshViewType : uint8_t
{
    Unknown,
    StaticMeshView,
    FrameMeshView
};
using GPUMeshViewHandle = Handle<GPUMeshViewType>;

using GPUHandle = std::variant<GPUResourceHandle, GPUViewHandle, GPUMeshViewHandle>;
}  // namespace rayvox



namespace std
{
template <typename T>
struct hash<rayvox::Handle<T>>
{
    size_t operator()(const rayvox::Handle<T>& g) const noexcept
    {
        return std::hash<uint64_t>{}(g._guid);
    }
};
}  // namespace std


