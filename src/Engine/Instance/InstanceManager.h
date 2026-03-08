#pragma once

#include <directx/d3d12.h>
#include "Components/EntityHandle.h"
#include "DirtyFlag.h"
#include "EigenTypes.h"
#include "Handles.h"
#include "Instance/InstanceKind.h"
#include "Renderer/EngineConfig.h"
#include "Renderer/ResourceManager.h"
#include "instanceDeclaration.h"

#include <emhash/hash_table8.hpp>
#include <entt/entt.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
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
    std::array<ComponentFlag, FramesInFlight> _dirtyComponentsByFrame;

    void assignAll(ComponentFlag flag)
    {
        for (auto& f : _dirtyComponentsByFrame)
        {
            f = flag;
        }
    }

    void clear(size_t frame) { _dirtyComponentsByFrame[frame] = ComponentFlag::None; }

    void setAll(ComponentFlag flag)
    {
        for (auto& f : _dirtyComponentsByFrame)
        {
            f |= flag;
        }
    }

    bool none()
    {
        for (auto& f : _dirtyComponentsByFrame)
        {
            if (f != ComponentFlag::None)
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

    FrameInstancePool(ResourceManager& rm, size_t initCapacity,
                      const std::string& name = "FrameInstancePool", bool dense = false)
        : _resourceManager(rm)
    {
        gpuPoolCapacity_ = initCapacity;
        _name = name;
        createGPUResourcesAndViews();
    }

    ResourceManager& _resourceManager;
    std::string _name;

    emhash8::HashMap<EntityHandle, GPUInstanceID> entityToId_;
    emhash8::HashMap<GPUInstanceID, EntityHandle> idToEntity_;
    emhash8::HashMap<EntityHandle, FrameDirtyFlag> dirtyComponents_;

    static constexpr ComponentFlag _instanceUsedComponentFlag = type::UsedComposents;

    GPUViewHandle _instancePoolViewHandle;

    GPUInstanceID insert(const EntityHandle& e)
    {
        if (auto it = entityToId_.find(e); it != entityToId_.end())
            return it->second;

        GPUInstanceID id = static_cast<uint32_t>(size());

        FrameDirtyFlag dirtyf;
        dirtyf.setAll(_instanceUsedComponentFlag);
        dirtyComponents_.emplace(e, dirtyf);

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
                dirtyf.setAll(_instanceUsedComponentFlag);
                dirtyComponents_[movedEntity] = dirtyf;
            }
        }

        entityToId_.erase(e);
        idToEntity_.erase(lastId);
        dirtyComponents_.erase(e);
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

    void createGPUResourcesAndViews()
    {
        if (_instancePoolViewHandle.valid())
        {
            _resourceManager.requestDestroy(_instancePoolViewHandle, true);
        }

        auto rhandle = _resourceManager.createBufferFrameResource(
            gpuPoolCapacity_ * sizeof(typename type::GPUData), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_HEAP_TYPE_DEFAULT, _name);

        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = static_cast<UINT>(gpuPoolCapacity_);
        desc.Buffer.StructureByteStride = static_cast<UINT>(sizeof(typename type::GPUData));
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        _instancePoolViewHandle =
            _resourceManager.createFrameView<D3D12_SHADER_RESOURCE_VIEW_DESC>(rhandle, desc);
    }

    void markAllinstanceDirty()
    {
        dirtyComponents_.clear();
        for (auto&& [handle, _] : entityToId_)
        {
            auto& flags = dirtyComponents_[handle] = FrameDirtyFlag();
            flags.setAll(_instanceUsedComponentFlag);
        }
    }

    bool ensureCapacity()
    {
        if (gpuPoolSize_ > gpuPoolCapacity_)
        {
            gpuPoolCapacity_ *= 2;
            createGPUResourcesAndViews();
            markAllinstanceDirty();
            return true;
        }
        return false;
    }
};

// Current design assumes one rendering aspect (InstanceKind) per entity.
// If one day we need true multi-aspect rendering (e.g. Mesh + Light on the same entity),
// we can switch to a per-component GPU pool model.
// That approach would remove InstanceKind routing and simplify upload logic.
struct GPUInstanceManager
{
    GPUInstanceManager(Context& ctx);

    void uploadRemainingFrameDirty(Context& ctx);
    void markDirty(const EntityHandle& handle, ComponentFlag componentFlag);

    ResourceManager& _resourceManager;
    FrameInstancePool<StaticMeshInstance> _meshInstancesPool{_resourceManager, 256,
                                                             "StaticMeshInstancePool"};

    FrameInstancePool<CameraInstance> _cameraInstancesPool{_resourceManager, 1,
                                                           "CameraInstancePool"};

    FrameInstancePool<PointLightInstance> pointLightInstancePool_{_resourceManager, 32,
                                                                  "pointLightInstancePool"};

    // prochaine fois que t'ajoutes un type d'instance note toutes les étapes pour voir ce qu'on
    // peut améliorer là le processus est trop long
};
}  // namespace batap
