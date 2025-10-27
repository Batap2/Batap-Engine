#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "magic_enum/magic_enum.hpp"

namespace rayvox
{
struct GPU_GUID
{
    enum class GPUObject
    {
        FrameResource,
        FrameView,
        StaticResource,
        StaticView
    } _type;
    uint64_t _guid;

    GPU_GUID() {};

    GPU_GUID(GPUObject type) : _type(type)
    {
        std::random_device rd;
        uint64_t high = static_cast<uint64_t>(rd()) << 32;
        uint64_t low = static_cast<uint64_t>(rd());
        _guid = high | low;
    }

    GPU_GUID(GPUObject type, const std::string& path) : _type(type)
    {
        uint64_t pathHash = std::hash<std::string>{}(path);
        _guid = static_cast<uint64_t>(_type) ^ (pathHash << 1);
    }

    bool operator==(const GPU_GUID& other) const
    {
        return _type == other._type && _guid == other._guid;
    }

    std::string toString()
    {
        return magic_enum::enum_name(_type).data() + std::to_string(_guid);
    }
};
}  // namespace rayvox

namespace std
{
template <>
struct hash<rayvox::GPU_GUID>
{
    size_t operator()(const rayvox::GPU_GUID& g) const noexcept
    {
        return std::hash<uint64_t>{}(g._guid) ^ (std::hash<int>{}(static_cast<int>(g._type)) << 1);
    }
};
}  // namespace std