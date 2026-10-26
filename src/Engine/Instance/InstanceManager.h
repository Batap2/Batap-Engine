#pragma once

#include "Components/EntityHandle.h"
#include "EigenTypes.h"
#include "Handles.h"
#include "InstanceDeclaration.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/ResourceManager.h"

#include <emhash/hash_table8.hpp>
#include <entt/entt.hpp>

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
    uint32_t value = std::numeric_limits<uint32_t>::max();

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
    static_assert(FramesInFlight <= 8);

    uint8_t bits_ = 0;

    void setAll() { bits_ = (uint8_t{1} << FramesInFlight) - 1; }

    void clear(size_t frame) { bits_ &= static_cast<uint8_t>(~(uint8_t{1} << frame)); }

    bool dirty(size_t frame) const { return (bits_ >> frame) & 1; }

    bool none() const { return bits_ == 0; }
};

template <GPUInstance Instance>
struct FrameInstancePool
{
    using InstanceType = Instance;

    explicit FrameInstancePool(ResourceManager& rm)
        : resourceManager_(rm),
          name_(refl::typeName<Instance>()),
          usedComponents_(usedComponentMask<Instance>())
    {
        gpuPoolCapacity_ = initialCapacityOf<Instance>();
        name_ += "Pool";
        createGPUResources();
    }

    ResourceManager& resourceManager_;
    std::string name_;

    emhash8::HashMap<EntityHandle, GPUInstanceID> entityToId_;
    emhash8::HashMap<GPUInstanceID, EntityHandle> idToEntity_;
    emhash8::HashMap<EntityHandle, FrameDirtyFlag> dirtyInstances_;

    // Set once the registry has handed out its indices, so a component change
    // routes to the pools whose Uses list names it.
    ComponentMask usedComponents_ = 0;

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

    bool contains(const EntityHandle& e) { return entityToId_.find(e) != entityToId_.end(); }

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
            gpuPoolCapacity_ * sizeof(typename Instance::GPUData), name_);
    }

    void markAllInstancesDirty()
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
            markAllInstancesDirty();
            return true;
        }
        return false;
    }
};

// One pool per entry of GPUInstances, addressed by instance type rather than by
// member name: generic code goes through forEach and never names a pool.
template <class KindList>
struct InstancePools;

template <class... Instances>
struct InstancePools<TypeList<Instances...>>
{
    // Repeats rm once per kind so the tuple builds each pool in place.
    template <class>
    static ResourceManager& sameRm(ResourceManager& rm)
    {
        return rm;
    }

    explicit InstancePools(ResourceManager& rm) : pools_{sameRm<Instances>(rm)...} {}

    std::tuple<FrameInstancePool<Instances>...> pools_;

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
};

// Assumes one rendering aspect per entity: holding two marker components would
// put an entity in two pools. True multi-aspect would mean a per-component pool
// model instead.
struct GPUInstanceManager
{
    GPUInstanceManager(Engine& ctx);
    ~GPUInstanceManager();

    void uploadRemainingFrameDirty(Engine& ctx);
    void markDirty(const EntityHandle& handle, ComponentMask changed);

    template <class Component>
    void markDirty(const EntityHandle& handle)
    {
        markDirty(handle, componentMask<Component>());
    }

    // Membership follows the marker component: emplacing one anywhere — factory,
    // deserializer, game code — puts the entity in its pool, and destroying the
    // component or the entity takes it out.
    void connectHooks(entt::registry& reg);

    template <class Instance>
    void onMarkerCreated(entt::registry& reg, entt::entity e)
    {
        pool<Instance>().insert(EntityHandle{&reg, e});
    }

    template <class Instance>
    void onMarkerDestroyed(entt::registry& reg, entt::entity e)
    {
        pool<Instance>().remove(EntityHandle{&reg, e});
    }

    template <class Instance>
    FrameInstancePool<Instance>& pool()
    {
        return pools_.get<Instance>();
    }

    template <class F>
    void forEachPool(F&& f)
    {
        pools_.forEach(std::forward<F>(f));
    }

    ResourceManager& resourceManager_;
    entt::registry* registry_ = nullptr;
    InstancePools<GPUInstances> pools_{resourceManager_};
};
}  // namespace batap
