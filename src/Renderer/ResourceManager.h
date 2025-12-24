#pragma once

#include <cstddef>
#include <optional>
#include <unordered_set>
#include <variant>
#include "Renderer/ResourceFormatWrapper.h"
#include "magic_enum/magic_enum.hpp"
#define NOMINMAX

#include <wrl/client.h>
#include "Renderer/includeDX12.h"

#include "AssertUtils.h"
#include "DescriptorHeapAllocator.h"
#include "FenceManager.h"
#include "Handles.h"
#include "ResourceName.h"

#include <intsafe.h>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>

struct DescriptorHeapAllocator;

namespace rayvox
{

template <class>
inline constexpr bool false_v = false;

struct GPUResource
{
    GPUResource(D3D12_RESOURCE_STATES initialState) { _currentState = initialState; }

    void transitionTo(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList,
                      D3D12_RESOURCE_STATES newState)
    {
        if (_currentState != newState)
        {
            CD3DX12_RESOURCE_BARRIER barrier =
                CD3DX12_RESOURCE_BARRIER::Transition(_resource.Get(), _currentState, newState);
            commandList->ResourceBarrier(1, &barrier);
            _currentState = newState;
        }
    }

    ID3D12Resource* get() { return _resource.Get(); }

    D3D12_RESOURCE_STATES getState() { return _currentState; }

    void setResource(D3D12_RESOURCE_STATES currentState,
                     std::string_view name = "Unnamed Empty Resource")
    {
        assert(_resource && "GPUResource::setResource called with null ID3D12Resource");

        _currentState = currentState;
        _init = true;
        std::wstring wname(name.begin(), name.end());
        _resource->SetName(wname.c_str());
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> _resource;
    bool _init = false;

   protected:
    D3D12_RESOURCE_STATES _currentState = D3D12_RESOURCE_STATE_COMMON;
};

struct GPUView
{
    GPUResource* _resource = nullptr;
    DescriptorHandle* _descriptorHandle = nullptr;
    enum class Type
    {
        CBV,
        SRV,
        UAV,
        RTV,
        DSV
    } _type;
};

struct GPUMeshView
{
    GPUResource* _resource = nullptr;

    enum class Type
    {
        Vertex,
        Index
    } _type;

    union
    {
        D3D12_VERTEX_BUFFER_VIEW vbv;
        D3D12_INDEX_BUFFER_VIEW ibv;
    };
};

struct ResourceManager
{
    ResourceManager(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, FenceManager& fenceManager,
                    uint8_t frameCount, uint32_t uploadBufferSize);

    static uint64_t AlignUp(uint64_t value, uint64_t alignment)
    {
        // alignment must be power of 2
        return (value + alignment - 1) & ~(alignment - 1);
    }

    struct UploadBuffer
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> _buffer;
        void* _mappedData = nullptr;
        uint64_t _currentOffset = 0;
        uint64_t _size = 0;
    };

    struct UploadRequest
    {
        const void* dataPtr() const { return _ownedData.empty() ? _data : _ownedData.data(); }
        uint64_t size() const { return _ownedData.empty() ? _dataSize : _ownedData.size(); }

        GPUHandle _guid;
        const void* _data;
        std::vector<std::byte> _ownedData;  // optional : for requestUploadOwned()
        uint64_t _dataSize;
        uint32_t _alignment;
        uint64_t _destinationOffset;
    };

    void requestUpload(GPUHandle guid, const void* data, uint64_t dataSize, uint32_t alignement,
                       uint64_t destinationOffset = 0);

    std::span<std::byte> requestUploadOwned(GPUHandle guid, uint64_t dataSize, uint32_t alignment,
                                            uint64_t destinationOffset = 0);

    void flushUploadRequests(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* commandQueue,
                             uint32_t frameIndex);

    void uploadToResource(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* commandQueue,
                          GPUResource* destination, const void* data, uint64_t dataSize,
                          uint32_t alignment, uint32_t frameIndex, uint64_t destinationOffset = 0);

    void updateResource(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* commandQueue,
                        const std::string_view& name, const void* data, uint64_t dataSize,
                        uint32_t alignment, uint32_t frameIndex, bool isFrameResource,
                        uint64_t destinationOffset = 0);

    void updateResource(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* commandQueue,
                        GPUHandle& handle, const void* data, uint64_t dataSize, uint32_t alignment,
                        uint32_t frameIndex, uint64_t destinationOffset = 0);

    // You must call setResource() of the GPUResource* after this
    GPUResourceHandle
    createEmptyStaticResource(std::optional<std::string_view> name = std::nullopt);
    GPUResourceHandle createEmptyFrameResource(std::optional<std::string_view> name = std::nullopt);

    GPUResourceHandle
    createBufferStaticResource(uint64_t size, D3D12_RESOURCE_STATES initialState,
                               D3D12_HEAP_TYPE heapType,
                               std::optional<std::string_view> name = std::nullopt,
                               D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

    GPUResourceHandle createBufferFrameResource(uint64_t size, D3D12_RESOURCE_STATES initialState,
                                                D3D12_HEAP_TYPE heapType,
                                                std::optional<std::string_view> name = std::nullopt,
                                                D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

    GPUResourceHandle
    createTexture2DStaticResource(uint32_t width, uint32_t height, DXGI_FORMAT format,
                                  D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState,
                                  D3D12_HEAP_TYPE heapType,
                                  std::optional<std::string_view> name = std::nullopt,
                                  D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

    GPUResourceHandle
    createTexture2DFrameResource(uint32_t width, uint32_t height, DXGI_FORMAT format,
                                 D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState,
                                 D3D12_HEAP_TYPE heapType,
                                 std::optional<std::string_view> name = std::nullopt,
                                 D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

    template <typename T>
    GPUViewHandle createStaticView(GPUResource* resource, T& viewDesc,
                                   std::optional<std::string_view> name = std::nullopt)
    {
        GPUViewHandle guid = generateGUID(GPUViewHandle::ObjectType::StaticView, name);
        GPUView view;
        DescriptorHeapAllocator& descriptorHeapAllocator = getHeapForDesc<T>();
        createSingleView(resource, &view, viewDesc, descriptorHeapAllocator);
        _staticViews[guid] = view;
        return guid;
    }

    template <typename T, std::ranges::range R, typename D>
        requires(std::same_as<D, T> ||
                 (std::ranges::range<D> && std::same_as<std::ranges::range_value_t<D>, T>) )
    GPUViewHandle createFrameView(const R& resources, D& viewDesc,
                                  std::optional<std::string_view> name = std::nullopt)
    {
        GPUViewHandle guid = generateGUID(GPUViewHandle::ObjectType::FrameView, name);

        _frameViews[guid] = std::vector<GPUView>();
        _frameViews[guid].reserve(_frameCount);

        DescriptorHeapAllocator& descriptorHeapAllocator = getHeapForDesc<T>();

        if constexpr (std::ranges::range<D> && !std::same_as<D, T>)
        {
            ThrowAssert(viewDesc.size() == resources.size(),
                        "resources and viewDesc must have the same size");

            size_t i = 0;
            for (auto resource : resources)
            {
                GPUView view;
                createSingleView(resource, &view, viewDesc[i++], descriptorHeapAllocator);
                _frameViews[guid].push_back(view);
            }
        }
        else
        {
            for (auto resource : resources)
            {
                GPUView view;
                createSingleView(resource, &view, viewDesc, descriptorHeapAllocator);
                _frameViews[guid].push_back(view);
            }
        }

        return guid;
    }

    // offset and size must be aligned to 256
    GPUViewHandle createStaticCBV(GPUResourceHandle resource_guid,
                                  std::optional<std::string_view> name = std::nullopt,
                                  uint64_t offset = 0, uint64_t size = 0);

    GPUViewHandle createFrameCBV(GPUResourceHandle resource_guid,
                                 std::optional<std::string_view> name = std::nullopt,
                                 uint64_t offset = 0, uint64_t size = 0);

    GPUMeshViewHandle createStaticIBV(GPUResourceHandle resource_guid,
                                      ResourceFormat format = ResourceFormat::R32_UINT,
                                      std::optional<std::string_view> name = std::nullopt,
                                      uint64_t offset = 0, uint64_t size = 0);

    GPUMeshViewHandle createStaticVBV(GPUResourceHandle resource_guid, uint32_t strideBytes,
                                      std::optional<std::string_view> name = std::nullopt,
                                      uint64_t offset = 0, uint64_t size = 0);

    void destroyGPUObject(GPUResourceHandle guid);
    void destroyGPUObject(GPUViewHandle guid);

    GPUResource* getStaticResource(RN n);
    std::vector<GPUResource*> getFrameResource(RN n);
    GPUView& getStaticView(RN n);
    std::vector<GPUView>& getFrameView(RN n);
    GPUMeshView& getStaticMeshView(RN n);

    GPUResource* getStaticResource(GPUResourceHandle& guid);
    std::vector<GPUResource*> getFrameResource(GPUResourceHandle& guid);
    GPUView& getStaticView(GPUViewHandle& guid);
    std::vector<GPUView>& getFrameView(GPUViewHandle& guid);
    GPUMeshView& getStaticMeshView(GPUMeshViewHandle& guid);

    // GPUHandle nameToGuid(const std::string name);
    GPUResourceHandle generateGUID(GPUResourceHandle::ObjectType type,
                                   std::optional<std::string_view> name = std::nullopt);
    GPUViewHandle generateGUID(GPUViewHandle::ObjectType type,
                               std::optional<std::string_view> name = std::nullopt);
    GPUMeshViewHandle generateGUID(GPUMeshViewHandle::ObjectType type,
                                   std::optional<std::string_view> name = std::nullopt);

    Microsoft::WRL::ComPtr<ID3D12Device2> _device;
    uint8_t _frameCount;

    // Frame resources: change every frame, duplicated per frame (e.g., constant
    // buffers)
    // Static resources: rarely updated, must wait for GPU before re-upload

    std::unordered_map<GPUResourceHandle, std::vector<std::unique_ptr<GPUResource>>> _frameResource;
    std::unordered_map<GPUResourceHandle, std::unique_ptr<GPUResource>> _staticResources;
    std::unordered_map<GPUViewHandle, std::vector<GPUView>> _frameViews;
    std::unordered_map<GPUViewHandle, GPUView> _staticViews;
    std::unordered_map<GPUMeshViewHandle, GPUMeshView> _staticMeshViews;

    FenceManager& _fenceManager;
    uint32_t _fenceId;

    std::vector<UploadBuffer> _uploadBuffers;

    DescriptorHeapAllocator _descriptorHeapAllocator_CBV_SRV_UAV;
    DescriptorHeapAllocator _descriptorHeapAllocator_SAMPLER;
    DescriptorHeapAllocator _descriptorHeapAllocator_RTV;
    DescriptorHeapAllocator _descriptorHeapAllocator_DSV;

   private:
    template <typename T>
    void createSingleView(GPUResource* resource, GPUView* view, T& viewDesc,
                          DescriptorHeapAllocator& descriptorHeapAllocator)
    {
        view->_resource = resource;

        if constexpr (std::is_same_v<T, D3D12_CONSTANT_BUFFER_VIEW_DESC>)
        {
            view->_descriptorHandle = descriptorHeapAllocator.alloc();
            view->_type = GPUView::Type::CBV;
            _device->CreateConstantBufferView(&viewDesc, view->_descriptorHandle->cpuHandle);
        }
        else if constexpr (std::is_same_v<T, D3D12_SHADER_RESOURCE_VIEW_DESC>)
        {
            view->_descriptorHandle = descriptorHeapAllocator.alloc();
            view->_type = GPUView::Type::SRV;
            _device->CreateShaderResourceView(resource->get(), &viewDesc,
                                              view->_descriptorHandle->cpuHandle);
        }
        else if constexpr (std::is_same_v<T, D3D12_UNORDERED_ACCESS_VIEW_DESC>)
        {
            view->_descriptorHandle = descriptorHeapAllocator.alloc();
            view->_type = GPUView::Type::UAV;
            _device->CreateUnorderedAccessView(resource->get(), nullptr, &viewDesc,
                                               view->_descriptorHandle->cpuHandle);
        }
        else if constexpr (std::is_same_v<T, D3D12_RENDER_TARGET_VIEW_DESC>)
        {
            view->_descriptorHandle = descriptorHeapAllocator.alloc();
            view->_type = GPUView::Type::RTV;
            _device->CreateRenderTargetView(resource->get(), &viewDesc,
                                            view->_descriptorHandle->cpuHandle);
        }
        else if constexpr (std::is_same_v<T, D3D12_DEPTH_STENCIL_VIEW_DESC>)
        {
            view->_descriptorHandle = descriptorHeapAllocator.alloc();
            view->_type = GPUView::Type::DSV;
            _device->CreateDepthStencilView(resource->get(), &viewDesc,
                                            view->_descriptorHandle->cpuHandle);
        }
    }

    template <typename Desc>
    DescriptorHeapAllocator& getHeapForDesc()
    {
        if constexpr (std::same_as<Desc, D3D12_RENDER_TARGET_VIEW_DESC>)
            return _descriptorHeapAllocator_RTV;
        else if constexpr (std::same_as<Desc, D3D12_DEPTH_STENCIL_VIEW_DESC>)
            return _descriptorHeapAllocator_DSV;
        else if constexpr (std::same_as<Desc, D3D12_SAMPLER_DESC>)
            return _descriptorHeapAllocator_SAMPLER;
        else if constexpr (std::same_as<Desc, D3D12_SHADER_RESOURCE_VIEW_DESC> ||
                           std::same_as<Desc, D3D12_UNORDERED_ACCESS_VIEW_DESC> ||
                           std::same_as<Desc, D3D12_CONSTANT_BUFFER_VIEW_DESC>)
            return _descriptorHeapAllocator_CBV_SRV_UAV;
        else
            static_assert(false_v<Desc>, "Unsupported DX12 descriptor type");
    }

    std::unordered_map<std::string, GPUResourceHandle> _nameToResourceGuidMap;
    std::unordered_map<std::string, GPUViewHandle> _nameToViewGuidMap;
    std::unordered_map<std::string, GPUMeshViewHandle> _nameToMeshViewGuidMap;

    std::unordered_set<GPUResourceHandle> _createdGPUResourceHandle;
    std::unordered_set<GPUViewHandle> _createdGPUViewHandle;
    std::unordered_set<GPUMeshViewHandle> _createdGPUMeshViewHandle;

    std::vector<UploadRequest> _uploadRequests;
};
}  // namespace rayvox
