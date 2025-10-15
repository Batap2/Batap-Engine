#pragma once

#include <entt/entt.hpp>

namespace rayvox
{
struct Entity
{
    Entity() : _handle(entt::null), _registry(nullptr)
    {
    }
    Entity(entt::entity handle, entt::registry* registry) : _handle(handle), _registry(registry)
    {
    }

    template <typename T, typename... Args>
    T& addComponent(Args&&... args)
    {
        return _registry->emplace<T>(_handle, std::forward<Args>(args)...);
    }

    template <typename T>
    T* getComponent()
    {
        return _registry->try_get<T>(_handle);
    }

    template <typename T>
    void removeComponent()
    {
        _registry->remove<T>(_handle);
    }

    template <typename T>
    bool hasComponent()
    {
        return _registry->all_of<T>(_handle);
    }

    void destroy()
    {
        _registry->destroy(_handle);
        _handle = entt::null;
    }

    bool isValid() const
    {
        return _registry && _registry->valid(_handle);
    }

    entt::entity getHandle() const
    {
        return _handle;
    }

    operator bool() const
    {
        return isValid();
    }
    operator entt::entity() const
    {
        return _handle;
    }

   private:
    entt::entity _handle;
    entt::registry* _registry;
};
}  // namespace rayvox