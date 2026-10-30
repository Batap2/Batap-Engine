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
#include <optional>
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

template <bool Packed>
struct InstanceSlots
{
    GPUInstanceID find(const EntityHandle& e) const
    {
        const auto it = entityToId_.find(e);
        return it != entityToId_.end() ? it->second : GPUInstanceID{};
    }

    GPUInstanceID insert(const EntityHandle& e)
    {
        GPUInstanceID id = static_cast<uint32_t>(end_);
        if (!Packed && !free_.empty())
        {
            id = free_.back();
            free_.pop_back();
        }
        else
            ++end_;
        entityToId_.emplace(e, id);
        if constexpr (Packed)
            idToEntity_.emplace(id, e);
        return id;
    }

    std::optional<EntityHandle> remove(const EntityHandle& e)
    {
        const GPUInstanceID removed = find(e);
        if (!removed.valid())
            return std::nullopt;
        entityToId_.erase(e);
        if constexpr (!Packed)
        {
            free_.push_back(removed);
            return std::nullopt;
        }
        else
        {
            const GPUInstanceID last = static_cast<uint32_t>(--end_);
            const EntityHandle moved = idToEntity_[last];
            idToEntity_.erase(last);
            if (removed == last)
                return std::nullopt;
            entityToId_[moved] = removed;
            idToEntity_[removed] = moved;
            return moved;
        }
    }

    size_t size() const { return end_; }

    template <class F>
    void forEach(F&& f) const
    {
        for (auto&& [handle, id] : entityToId_)
            f(handle, id);
    }

   private:
    emhash8::HashMap<EntityHandle, GPUInstanceID> entityToId_;
    emhash8::HashMap<GPUInstanceID, EntityHandle> idToEntity_;
    std::vector<GPUInstanceID> free_;
    size_t end_ = 0;
};

template <GPUInstance Instance>
struct FrameInstancePool
{
    using InstanceType = Instance;
    static_assert(
        !KeepsHistory<Instance> || !requires { Instance::CountField; },
        "a packed pool moves its instances, and their history would stay behind");

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

    emhash8::HashMap<EntityHandle, FrameDirtyFlag> dirtyInstances_;
    std::vector<std::optional<typename Instance::GPUData>> previous_;

    // Set once the registry has handed out its indices, so a component change
    // routes to the pools whose Uses list names it.
    ComponentMask usedComponents_ = 0;

    GPUResourceHandle instancePoolHandle_;

    GPUInstanceID insert(const EntityHandle& e)
    {
        if (const GPUInstanceID id = slots_.find(e); id.valid())
            return id;
        const GPUInstanceID id = slots_.insert(e);
        if constexpr (KeepsHistory<Instance>)
        {
            if (previous_.size() <= id)
                previous_.resize(id + 1);
            previous_[id].reset();
        }
        dirtyInstances_[e].setAll();
        ensureCapacity();
        return id;
    }

    void remove(const EntityHandle& e)
    {
        if (!e.valid())
            return;
        if (const std::optional<EntityHandle> moved = slots_.remove(e))
            dirtyInstances_[*moved].setAll();
        dirtyInstances_.erase(e);
    }

    bool contains(const EntityHandle& e) const { return slots_.find(e).valid(); }

    GPUInstanceID getGPUIndex(const EntityHandle& e) const { return slots_.find(e); }

    size_t size() const { return slots_.size(); }
    size_t capacity() const { return gpuPoolCapacity_; }

   private:
    InstanceSlots<requires { Instance::CountField; }> slots_;
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
        slots_.forEach([&](const EntityHandle& handle, GPUInstanceID)
                       { dirtyInstances_[handle].setAll(); });
    }

    bool ensureCapacity()
    {
        if (slots_.size() > gpuPoolCapacity_)
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
