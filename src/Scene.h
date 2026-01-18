#pragma once

#include "Components/EntityHandle.h"
#include "Instance/InstanceManager.h"
#include "Components/ComponentToFlag.h"

#include <entt/entt.hpp>

namespace rayvox
{
struct Scene
{
    Scene(GPUInstanceManager& instanceManager) : _instanceManager(instanceManager) {}
    virtual ~Scene() = default;

    virtual void update(float deltaTime) {}

    entt::registry _registry;

    template <class T>
    struct WriteProxy
    {
        entt::registry* r{};
        entt::entity e{entt::null};
        GPUInstanceManager* _instanceManager;
        T* ptr{};

        T* operator->() { return ptr; }
        T& get() { return *ptr; }

        ~WriteProxy()
        {
            _instanceManager->markDirty(EntityHandle(e, r), ComponentToFlag<T>::value);
        }
    };

    template <class T>
    WriteProxy<T> write(const entt::registry& r, const entt::entity& e)
    {
        return WriteProxy<T>{&r, e, &_instanceManager, &r.get<T>(e)};
    }

    template <class T>
    WriteProxy<T> write(const EntityHandle& handle)
    {
        return WriteProxy<T>{&handle._reg, handle._entity, &_instanceManager, &handle._reg->get<T>(handle._entity)};
    }

   private:
    GPUInstanceManager& _instanceManager;
};
}  // namespace rayvox
