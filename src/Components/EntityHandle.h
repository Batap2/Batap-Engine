#pragma once

#include <entt/entt.hpp>
#include <functional>

namespace rayvox
{
struct EntityHandle
{
    const entt::entity _entity;
    const entt::registry* _reg;

    EntityHandle(entt::entity entity, const entt::registry* reg) noexcept
        : _entity(entity), _reg(reg)
    {}

    bool operator==(const EntityHandle& other) const noexcept
    {
        return _entity == other._entity && _reg == other._reg;
    }
};
}  // namespace rayvox

namespace std
{
template <>
struct hash<rayvox::EntityHandle>
{
    std::size_t operator()(const rayvox::EntityHandle& e) const noexcept
    {
        std::size_t h1 = std::hash<entt::entity>{}(e._entity);
        std::size_t h2 = std::hash<const entt::registry*>{}(e._reg);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
}  // namespace std
