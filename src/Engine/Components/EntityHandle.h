#pragma once

#include <entt/entt.hpp>
#include <functional>
#include <utility>
#include "DebugUtils.h"
#include "EigenTypes.h"
#include "Reflection/ComponentMask.h"

namespace batap
{

enum class Space
{
    Local,
    Parent,
    World
};

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

    template <typename T, typename... Args>
    T& emplace(Args&&... args)
    {
        ThrowAssert(valid(), "entityHandle not valid");
        return reg_->emplace<T>(entity_, std::forward<Args>(args)...);
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
    const T* try_get() const noexcept
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

    template <typename T>
    const T& get() const noexcept
    {
        ThrowAssert(valid(), "entityHandle not valid");
        return reg_->get<T>(entity_);
    }

    // A direct get<T>() write never reaches the GPU
    template <typename T>
    T& write()
    {
        markDirty(componentMask<T>());
        return get<T>();
    }

    void markDirty(ComponentMask changed);

    void setLocalPosition(const v3f& p);
    void setLocalRotation(const quatf& q);
    void setLocalScale(const v3f& s);
    void setPosition(const v3f& p, Space space = Space::World);
    void setRotation(const quatf& q, Space space = Space::World);

    void translate(const v3f& vec, Space space = Space::Local);
    void rotate(const quatf& delta, Space space = Space::Local);
    void rotate(const v3f& axis, float radians, Space space = Space::Local);
    void scale(const v3f& vec);

    void setParent(EntityHandle newParent);
    EntityHandle parent() const;

    void addForce(const v3f& force);
    void addForce(const v3f& force, const v3f& worldPoint);
    void addTorque(const v3f& torque);
    void addImpulse(const v3f& impulse);
    void addImpulse(const v3f& impulse, const v3f& worldPoint);
    void addAngularImpulse(const v3f& angularImpulse);

    v3f velocity() const;
    void setVelocity(const v3f& v);
    v3f angularVelocity() const;
    void setAngularVelocity(const v3f& v);
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
