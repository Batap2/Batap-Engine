#pragma once

#include "Components/EntityHandle.h"
#include "DirtyFlag.h"
#include "EigenTypes.h"
#include "Handles.h"
#include "Instance/EntityKind.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/ResourceManager.h"
#include "instanceDeclaration.h"

#include <emhash/hash_table8.hpp>
#include <entt/entt.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace batap
{
struct GPUInstanceID
{
    uint32_t value = 0;

    GPUInstanceID() = default;
    GPUInstanceID(uint32_t v) : value(v) {}

    bool valid() const { return value != std::numeric_limits<uint32_t>::max(); }

    operator uint32_t() const { return value; }
};

}  // namespace batap

namespace std
{
template <>
struct hash<batap::GPUInstanceID>
{
    std::size_t operator()(const batap::GPUInstanceID& id) const noexcept
    {
        return std::hash<uint32_t>{}(id.value);
    }
};
}  // namespace std

namespace batap
{

struct FrameDirtyFlag
{
    std::array<bool, FramesInFlight> dirtyByFrame_ {};

    void setAll() { dirtyByFrame_.fill(true); }

    void clear(size_t frame) { dirtyByFrame_[frame] = false; }

    bool dirty(size_t frame) const { return dirtyByFrame_[frame]; }

    bool none() const
    {
        for (bool f : dirtyByFrame_)
        {
            if (f)
            {
                return false;
            }
        }
        return true;
    }
};

template <typename T>
concept HasUsedComponents = requires {
    { T::UsedComposents } -> std::convertible_to<ComponentFlag>;
};

template <typename type>
struct FrameInstancePool
{
    static_assert(requires { typename type::GPUData; });
    static_assert(HasUsedComponents<type>);
    using InstanceType = type;

    explicit FrameInstancePool(ResourceManager& rm) : resourceManager_(rm)
    {
        gpuPoolCapacity_ = type::InitialCapacity;
        name_ = type::PoolName;
        createGPUResources();
    }

    ResourceManager& resourceManager_;
    std::string name_;

    emhash8::HashMap<EntityHandle, GPUInstanceID> entityToId_;
    emhash8::HashMap<GPUInstanceID, EntityHandle> idToEntity_;
    emhash8::HashMap<EntityHandle, FrameDirtyFlag> dirtyInstances_;

    static constexpr ComponentFlag instanceUsedComponentFlag_ = type::UsedComposents;

    GPUResourceHandle instancePoolHandle_;

    GPUInstanceID insert(const EntityHandle& e)
    {
        if (auto it = entityToId_.find(e); it != entityToId_.end())
            return it->second;

        GPUInstanceID id = static_cast<uint32_t>(size());

        FrameDirtyFlag dirtyf;
        dirtyf.setAll();
        dirtyInstances_.emplace(e, dirtyf);

        idToEntity_.emplace(id, e);
        entityToId_.emplace(e, id);

        gpuPoolSize_++;
        ensureCapacity();

        return id;
    }

    void remove(const EntityHandle& e)
    {
        if (size() == 0 || !e.valid())
            return;
        auto it = entityToId_.find(e);
        if (it == entityToId_.end())
            return;
        GPUInstanceID removedId = it->second;
        GPUInstanceID lastId{static_cast<uint32_t>(size() - 1)};

        if (removedId != lastId)
        {
            auto lastIt = idToEntity_.find(lastId);
            if (lastIt != idToEntity_.end())
            {
                EntityHandle movedEntity = lastIt->second;

                entityToId_[movedEntity] = removedId;
                idToEntity_[removedId] = movedEntity;

                FrameDirtyFlag dirtyf;
                dirtyf.setAll();
                dirtyInstances_[movedEntity] = dirtyf;
            }
        }

        entityToId_.erase(e);
        idToEntity_.erase(lastId);
        dirtyInstances_.erase(e);
        gpuPoolSize_--;
    }

    GPUInstanceID getGPUIndex(const EntityHandle& e)
    {
        if (auto it = entityToId_.find(e); it != entityToId_.end())
        {
            return it->second;
        }

        return std::numeric_limits<uint32_t>::max();
    }

    size_t size() const { return gpuPoolSize_; }
    size_t capacity() const { return gpuPoolCapacity_; }

   private:
    size_t gpuPoolSize_ = 0;
    size_t gpuPoolCapacity_ = 1;

    void createGPUResources()
    {
        if (instancePoolHandle_.valid())
        {
            resourceManager_.requestDestroy(instancePoolHandle_);
        }

        instancePoolHandle_ = resourceManager_.createPerFrameBuffer(
            gpuPoolCapacity_ * sizeof(typename type::GPUData), name_);
    }

    void markAllinstanceDirty()
    {
        dirtyInstances_.clear();
        for (auto&& [handle, _] : entityToId_)
        {
            dirtyInstances_[handle].setAll();
        }
    }

    bool ensureCapacity()
    {
        if (gpuPoolSize_ > gpuPoolCapacity_)
        {
            gpuPoolCapacity_ *= 2;
            createGPUResources();
            markAllinstanceDirty();
            return true;
        }
        return false;
    }
};

// One pool per entry of GPUKinds, addressed by instance type rather than by
// member name: generic code goes through forEach/visit and never names a pool.
template <class KindList>
struct InstancePools;

template <class... Ks>
struct InstancePools<TypeList<Ks...>>
{
    // Repeats rm once per kind so the tuple builds each pool in place.
    template <class>
    static ResourceManager& sameRm(ResourceManager& rm)
    {
        return rm;
    }

    explicit InstancePools(ResourceManager& rm) : pools_{sameRm<Ks>(rm)...} {}

    std::tuple<FrameInstancePool<typename Ks::InstanceType>...> pools_;

    template <class Instance>
    FrameInstancePool<Instance>& get()
    {
        return std::get<FrameInstancePool<Instance>>(pools_);
    }

    template <class F>
    void forEach(F&& f)
    {
        std::apply([&](auto&... p) { (f(p), ...); }, pools_);
    }

    // A kind with no GPU instance matches nothing, so callers need neither a
    // switch nor a default case.
    template <class F>
    void visit(EntityKind kind, F&& f)
    {
        std::apply([&](auto&... p)
                   { ((Ks::kind == kind ? static_cast<void>(f(p)) : void()), ...); }, pools_);
    }
};

// Assumes one rendering aspect per entity. True multi-aspect (Mesh + Light on
// the same entity) would mean a per-component pool model instead, dropping the
// kind routing.
struct GPUInstanceManager
{
    GPUInstanceManager(Engine& ctx);

    void uploadRemainingFrameDirty(Engine& ctx);
    void markDirty(const EntityHandle& handle, ComponentFlag componentFlag);

    template <class Instance>
    FrameInstancePool<Instance>& pool()
    {
        return pools_.get<Instance>();
    }

    template <class F>
    void visitPool(EntityKind kind, F&& f)
    {
        pools_.visit(kind, std::forward<F>(f));
    }

    ResourceManager& resourceManager_;
    InstancePools<GPUKinds> pools_{resourceManager_};
};
}  // namespace batap
