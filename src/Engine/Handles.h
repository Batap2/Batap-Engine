#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "magic_enum/magic_enum.hpp"

namespace batap
{

template <typename TypeEnum>
    requires std::is_enum_v<TypeEnum>
struct Handle
{
    using ObjectType = TypeEnum;
    Handle() = default;

    explicit Handle(ObjectType type) : type_(type), guid_(random64()) {}

    Handle(ObjectType type, std::string_view name)
        : type_(type), guid_(hash64(name) ^ (uint64_t(type) * 0x9e3779b97f4a7c15ull))
    {}

    bool valid() const { return guid_ != 0; }

    bool operator==(const Handle& other) const
    {
        return type_ == other.type_ && guid_ == other.guid_;
    }

    std::string toString() const
    {
        std::string s(magic_enum::enum_name(type_));
        s += ":";
        s += std::to_string(guid_);
        return s;
    }

    ObjectType type_{};
    uint64_t guid_ = 0;

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

}  // namespace batap

namespace std
{
template <typename T>
struct hash<batap::Handle<T>>
{
    size_t operator()(const batap::Handle<T>& g) const noexcept
    {
        return std::hash<uint64_t>{}(g.guid_);
    }
};
}  // namespace std
