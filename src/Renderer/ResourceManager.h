#pragma once

#include <concepts>
#include <string_view>
#define NOMINMAX
#include <wrl.h>
#include <wrl/client.h>

#include <cstdint>
#include "DirectX-Headers/include/directx/d3d12.h"

using namespace Microsoft::WRL;

#include "DescriptorHeapAllocator.h"
#include "FenceManager.h"

#include <iostream>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>


namespace rayvox
{

struct DescriptorHeapAllocator;

struct GPUResource
{
    GPUResource(D3D12_RESOURCE_STATES initialState)
    {
        _currentState = initialState;
    }

    ComPtr<ID3D12Resource> _resource;
    bool _init = false;

    void transitionTo(ComPtr<ID3D12GraphicsCommandList> commandList, D3D12_RESOURCE_STATES newState)
    {
        if (_currentState != newState)
        {
            CD3DX12_RESOURCE_BARRIER barrier =
                CD3DX12_RESOURCE_BARRIER::Transition(_resource.Get(), _currentState, newState);
            commandList->ResourceBarrier(1, &barrier);
            _currentState = newState;
        }
    }

    ID3D12Resource* get()
    {
        return _resource.Get();
    }

    D3D12_RESOURCE_STATES getState()
    {
        return _currentState;
    }

    void setResource(D3D12_RESOURCE_STATES currentState, std::string name = "unnamed_resource")
    {
        _currentState = currentState;
        std::wstring wname(name.begin(), name.end());
        _resource->SetName(wname.c_str());
        _init = true;
    };

   protected:
    D3D12_RESOURCE_STATES _currentState = D3D12_RESOURCE_STATE_COMMON;
};

struct GPUView
{
    GPUResource* _resource;
    DescriptorHandle* _descriptorHandle;
    enum class Type
    {
        CBV,
        SRV,
        UAV,
        RTV,
        DSV
    } _type;
};

struct ResourceManager
{
    ResourceManager(const ComPtr<ID3D12Device2>& device, FenceManager& fenceManager,
                    uint8_t frameCount, uint32_t uploadBufferSize);

    uint64_t AlignUp(uint64_t value, uint64_t alignment)
    {
        // alignment must be power of 2
        return (value + alignment - 1) & ~(alignment - 1);
    }

    struct UploadBuffer
    {
        ComPtr<ID3D12Resource> _buffer;
        void* _mappedData = nullptr;
        uint64_t _currentOffset = 0;
        uint64_t _size = 0;
    };
    std::vector<UploadBuffer> uploadBuffers;

    void uploadToResource(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* commandQueue,
                          GPUResource* destination, void* data, uint64_t dataSize,
                          uint32_t alignment, uint32_t frameIndex, uint64_t destinationOffset = 0);

    void updateResource(ID3D12GraphicsCommandList* cmdList, ID3D12CommandQueue* commandQueue,
                        const std::string& name, void* data, uint64_t dataSize, uint32_t alignment,
                        uint32_t frameIndex, bool isFrameResource, uint64_t destinationOffset = 0);

    // You must call setResource() of the GPUResource* after this
    GPUResource* createEmptyResource();

    GPUResource* createBufferResource(uint64_t size, D3D12_RESOURCE_STATES initialState,
                                      D3D12_HEAP_TYPE heapType, std::string name = "unnamed_buffer",
                                      D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

    GPUResource* createTexture2DResource(uint32_t width, uint32_t height, DXGI_FORMAT format,
                                         D3D12_RESOURCE_FLAGS flags,
                                         D3D12_RESOURCE_STATES initialState,
                                         D3D12_HEAP_TYPE heapType,
                                         std::string name = "unnamed_texture",
                                         D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE);

    template <typename T>
    void createView(std::string name, GPUResource* resource, T& viewDesc,
                    DescriptorHeapAllocator& descriptorHeapAllocator)
    {
        GPUView view;
        createSingleView(resource, &view, viewDesc, descriptorHeapAllocator);
        _staticViews[name] = view;
    }

    template <typename T, std::ranges::range R>
        requires std::same_as<std::ranges::range_value_t<R>, GPUResource*>
    void createView(std::string name, const R& resources, T& viewDesc,
                    DescriptorHeapAllocator& descriptorHeapAllocator)
    {
        _frameViews[name] = std::vector<GPUView>();

        for (auto* resource : resources)
        {
            GPUView view;
            createSingleView(resource, &view, viewDesc, descriptorHeapAllocator);
            _frameViews[name].push_back(view);
        }
    }

    void createCBV(std::string name, GPUResource* resource, DescriptorHeapAllocator& allocator);

    ComPtr<ID3D12Device2> _device;
    uint8_t _frameCount;
    std::vector<std::unique_ptr<GPUResource>> _resources;

    // Frame resources: change every frame, duplicated per frame (e.g., constant
    // buffers)
    // Static resources: rarely updated, must wait for GPU before re-upload

    std::unordered_map<std::string, std::vector<std::unique_ptr<GPUResource>>> _frameResource;
    std::unordered_map<std::string, std::unique_ptr<GPUResource>> _staticResources;
    std::unordered_map<std::string, std::vector<GPUView>> _frameViews;
    std::unordered_map<std::string, GPUView> _staticViews;

    FenceManager& _fenceManager;
    uint32_t _fenceId;

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
    };
};
}  // namespace rayvox