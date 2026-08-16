#pragma once

#include <entt/entt.hpp>
#include <functional>
#include "DebugUtils.h"

namespace batap
{
struct EntityHandle
{
    entt::registry* reg_ = nullptr;
    entt::entity entity_ = entt::null;

    EntityHandle() = default;

    EntityHandle(entt::registry* reg, entt::entity entity) noexcept
    {
        reg_ = reg;
        entity_ = entity;
    }

    bool operator==(const EntityHandle& other) const noexcept
    {
        return entity_ == other.entity_ && reg_ == other.reg_;
    }

    template <typename T>
    T& emplace() 
    {
        ThrowAssert(valid(), "entityHandle not valid");
        return reg_->emplace<T>(entity_);
    }

    bool valid() const { return entity_ != entt::null && reg_ != nullptr && reg_->valid(entity_); }

    template <typename T>
    T* try_get() noexcept
    {
        if (!valid())
            return nullptr;

        return reg_->try_get<T>(entity_);
    }

    template <typename T>
    T& get() noexcept
    {
        ThrowAssert(valid(), "entityHandle not valid");
        return reg_->get<T>(entity_);
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
        std::size_t h1 = std::hash<entt::entity>{}(e.entity_);
        std::size_t h2 = std::hash<const entt::registry*>{}(e.reg_);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
}  // namespace std
