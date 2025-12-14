#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "magic_enum/magic_enum.hpp"

namespace rayvox
{
struct GPUHandle
{
    enum class GPUObject : uint8_t
    {
        FrameResource,
        FrameView,
        StaticResource,
        StaticView,
        Unknown
    };

    GPUHandle() = default;

    explicit GPUHandle(GPUObject type) : _type(type), _guid(random64()) {}

    GPUHandle(GPUObject type, std::string_view path)
        : _type(type), _guid(hash64(path) ^ (uint64_t(type) * 0x9e3779b97f4a7c15ull))
    {}

    bool operator==(const GPUHandle& other) const
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

    GPUObject _type = GPUObject::Unknown;
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
        for (unsigned char c : s)
        {
            h ^= uint64_t(c);
            h *= 1099511628211ull;
        }
        return h;
    }
};
}  // namespace rayvox

namespace std
{
template <>
struct hash<rayvox::GPUHandle>
{
    size_t operator()(const rayvox::GPUHandle& g) const noexcept
    {
        return std::hash<uint64_t>{}(g._guid);
    }
};
}  // namespace std