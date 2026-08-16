#pragma once

#include "Components/EntityHandle.h"
#include "Instance/InstanceManager.h"
#include "Reflection/ComponentRegistry.h"

#include <entt/entt.hpp>

namespace batap
{

struct World;
struct Engine;

struct Scene
{
    Scene(GPUInstanceManager& instanceManager);
    virtual ~Scene() = default;

    virtual void update(float deltaTime, Engine& ctx, World& world) {}

    entt::registry registry_;

    template <class T>
    struct WriteProxy
    {
        entt::registry* r = nullptr;
        entt::entity e{entt::null};
        GPUInstanceManager* instanceManager = nullptr;
        T* ptr = nullptr;

        WriteProxy() = default;
        WriteProxy(entt::registry* r_, entt::entity e_, GPUInstanceManager* im_, T* ptr_) noexcept
            : r(r_), e(e_), instanceManager(im_), ptr(ptr_)
        {}

        // must not be copied, else commit() could be called several times
        WriteProxy(const WriteProxy&) = delete;
        WriteProxy& operator=(const WriteProxy&) = delete;

        void commit() noexcept
        {
            // componentFlag<T>() is None for a CPU-only component, and
            // markDirty ignores it — writing to one is not an error.
            if (ptr && instanceManager)
            {
                instanceManager->markDirty(EntityHandle(r, e), componentFlag<T>());
            }
            // avoid double commit
            ptr = nullptr;
        }

        // Used when the proxy is returned by value (scene.write<T>())
        // or explicitly moved.
        // Transfers the responsibility of calling commit() to this object.
        WriteProxy(WriteProxy&& other) noexcept
            : r(other.r), e(other.e), instanceManager(other.instanceManager), ptr(other.ptr)
        {
            other.ptr = nullptr;
        }

        // Used when an existing proxy is replaced by another one.
        // First commits the old proxy, then takes ownership of the new one.
        WriteProxy& operator=(WriteProxy&& other) noexcept
        {
            if (this != &other)
            {
                commit();
                r = other.r;
                e = other.e;
                instanceManager = other.instanceManager;
                ptr = other.ptr;
                other.ptr = nullptr;
            }
            return *this;
        }

        explicit operator bool() const { return ptr != nullptr; }

        T* operator->() { return ptr; }
        T& get() { return *ptr; }

        ~WriteProxy() { commit(); }
    };

    template <class T>
    WriteProxy<T> write(entt::registry& r, entt::entity e)
    {
        if (auto* ptr = r.try_get<T>(e))
        {
            return WriteProxy<T>{&r, e, &instanceManager_, ptr};
        }

        return {};
    }

    template <class T>
    WriteProxy<T> write(const EntityHandle& handle)
    {
        auto& reg = *handle.reg_;

        if (auto* ptr = reg.try_get<T>(handle.entity_))
        {
            return WriteProxy<T>{&reg, handle.entity_, &instanceManager_, ptr};
        }

        return {};
    }

    GPUInstanceManager& instanceManager_;
};
}  // namespace batap
