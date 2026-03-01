#pragma once

#include <entt/entt.hpp>
#include <functional>
#include "DebugUtils.h"

namespace batap
{
struct EntityHandle
{
    entt::registry* _reg = nullptr;
    entt::entity _entity = entt::null;

    EntityHandle() = default;

    EntityHandle(entt::registry* reg, entt::entity entity) noexcept
    {
        _reg = reg;
        _entity = entity;
    }

    bool operator==(const EntityHandle& other) const noexcept
    {
        return _entity == other._entity && _reg == other._reg;
    }

    bool valid() const { return _entity != entt::null && _reg != nullptr && _reg->valid(_entity); }

    template <typename T>
    T* try_get() noexcept
    {
        if (!valid())
            return nullptr;

        return _reg->try_get<T>(_entity);
    }

    template <typename T>
    T& get() noexcept
    {
        ThrowAssert(valid(), "entityHandle not valid");
        return _reg->get<T>(_entity);
    }
};
}  // namespace batap

namespace std
{
template <>
struct hash<batap::EntityHandle>
{
    std::size_t operator()(const batap::EntityHandle& e) const noexcept
    {
        std::size_t h1 = std::hash<entt::entity>{}(e._entity);
        std::size_t h2 = std::hash<const entt::registry*>{}(e._reg);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
}  // namespace std
