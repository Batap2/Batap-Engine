#pragma once
#include <cstdint>
#include <random>
#include <string>

namespace rayvox
{
enum class AssetType : uint8_t
{
    Mesh,
    Texture,
    Material,
    Shader,
    Unknown
};

struct Assethandle
{
    AssetType _type;
    uint64_t _guid;

    bool operator==(const Assethandle&) const = default;
};
}  // namespace rayvox

namespace std
{
template <>
struct hash<rayvox::Assethandle>
{
    size_t operator()(const rayvox::Assethandle& g) const noexcept
    {
        return std::hash<uint64_t>{}(g._guid) ^ (std::hash<int>{}(static_cast<int>(g._type)) << 1);
    }
};
}  // namespace std