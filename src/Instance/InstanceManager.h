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

#include <array>
#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rayvox
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
            unsigned long a;
            _BitScanForward(&a, a);
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
    using Id = uint32_t;

    FrameInstancePool(ResourceManager& rm, size_t initPoolSize, const std::string& name = "FrameInstancePool") : _resourceManager(rm)
    {
        _gpuPoolSize = initPoolSize;
        _name = name;
    }

    ResourceManager& _resourceManager;
    std::string _name;

    size_t _gpuPoolSize = 0;
    std::vector<type> _pool;
    std::vector<Id> _freeList;
    entt::dense_map<EntityHandle, Id> _byEntity;
    entt::dense_map<EntityHandle, FrameDirtyFlag> _dirtyComponents;

    static constexpr ComponentFlag _instanceUsedComponentFlag = type::OwnedFlag;

    GPUViewHandle _instancePoolViewHandle;

    Id assign(const EntityHandle& e, const entt::registry& r)
    {
        if (auto it = _byEntity.find(e); it != _byEntity.end())
            return it->second;

        Id id = acquire();
        _byEntity[e] = id;

        _pool[id]._gpuIndex = id;

        auto [it, inserted] = _dirtyComponents.emplace(e, FrameDirtyFlag{});
        FrameDirtyFlag& flag = it->second;
        flag.assignAll(_instanceUsedComponentFlag);

        return id;
    }

    void remove(const EntityHandle& e)
    {
        _dirtyComponents.erase(e);
        auto it = _byEntity.find(e);
        if (it == _byEntity.end())
            return;
        release(it->second);
        _byEntity.erase(it);
    }

    type& get(const EntityHandle& e) { return _pool.at(_byEntity.at(e)); }
    type* getOrNull(const EntityHandle& e)
    {
        if (auto it = _byEntity.find(e); it != _byEntity.end())
        {
            return &_pool.at(it->second);
        }
        return nullptr;
    }

   private:
    Id acquire()
    {
        Id id;
        if (!_freeList.empty())
        {
            id = _freeList.back();
            _freeList.pop_back();
            _pool[id] = type{};
        }
        else
        {
            _pool.emplace_back();
            id = static_cast<Id>(_pool.size() - 1);
        }
        return id;
    }

    void release(Id id)
    {
        assert(id < _pool.size());
        _pool[id] = type{};
        _freeList.push_back(id);
    }

    type& get(Id id)
    {
        assert(id < _pool.size());
        return _pool[id];
    }

    const type& get(Id id) const
    {
        assert(id < _pool.size());
        return _pool[id];
    }

    size_t capacity() const { return _pool.size(); }

    void createGPUResourcesAndViews(){
        _resourceManager.createBufferFrameResource(_gpuPoolSize, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_DEFAULT, _name);
    }

    void uploadAllInstances(){
        for(auto& instance : _pool){
            // matk all instance dirty
        }
    }

    bool ensureCapacity()
    {
        if (_pool.size() > _gpuPoolSize)
        {
            _resourceManager.requestDestroy(_instancePoolViewHandle);
            createGPUResourcesAndViews();
            uploadAllInstances();
        }
    }
};

struct GPUInstanceManager
{
    GPUInstanceManager(ResourceManager& rm);

    void uploadRemainingFrameDirty(uint8_t frameIndex);
    void markDirty(const EntityHandle& handle, ComponentFlag componentFlag);

    ResourceManager& _resourceManager;
    FrameInstancePool<StaticMeshInstance> _meshInstancesPool{256};
};
};  // namespace rayvox
