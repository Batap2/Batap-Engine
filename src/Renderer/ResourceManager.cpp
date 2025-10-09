#include "ResourceManager.h"


#include <wrl/client.h>


#include <cstdint>

#include "CommandQueue.h"
#include "FenceManager.h"

#include <algorithm>
#include <stdexcept>

#include "DirectX-Headers/include/directx/d3d12.h"

namespace rayvox
{
ResourceManager::ResourceManager(const ComPtr<ID3D12Device2>& device, FenceManager& fenceManager,
                                 uint8_t frameCount, uint32_t uploadBufferSize)
    : _device(device), _frameCount(frameCount), _fenceManager(fenceManager)
{
    for (int i = 0; i < frameCount; ++i)
    {
        auto& buffer = uploadBuffers.emplace_back();

        buffer._size = AlignUp(uploadBufferSize, 256);
        buffer._currentOffset = 0;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Alignment = 0;
        bufferDesc.Width = buffer._size;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.SampleDesc.Quality = 0;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,  // upload buffer CPU-visible
            nullptr, IID_PPV_ARGS(&buffer._buffer));

        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to create upload buffer");
        }

        D3D12_RANGE readRange = {};  // We do not intend to read from this buffer
        hr = buffer._buffer->Map(0, &readRange, &buffer._mappedData);
        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to map upload buffer");
        }

        _fenceId = _fenceManager.createFence("Fence_ResourceManager");
    }
}

void ResourceManager::uploadToResource(ID3D12GraphicsCommandList* cmdList,
                                       ID3D12CommandQueue* commandQueue, GPUResource* destination,
                                       void* data, uint64_t dataSize, uint32_t alignment,
                                       uint32_t frameIndex, uint64_t destinationOffset)
{
    auto& upload = uploadBuffers[frameIndex];

    upload._currentOffset = AlignUp(upload._currentOffset, alignment);

    uint64_t remainingData = AlignUp(dataSize, alignment);
    uint64_t dataOffset = 0;

    while (remainingData)
    {
        uint64_t chunkSize = std::min(upload._size - upload._currentOffset, remainingData);

        memcpy(static_cast<uint8_t*>(upload._mappedData) + upload._currentOffset,
               static_cast<uint8_t*>(data) + dataOffset, chunkSize);
        cmdList->CopyBufferRegion(destination->get(), destinationOffset, upload._buffer.Get(),
                                  upload._currentOffset, chunkSize);

        destinationOffset += chunkSize;
        dataOffset += chunkSize;
        upload._currentOffset += chunkSize;
        if (upload._currentOffset >= upload._size)
        {
            upload._currentOffset = 0;
        }
        remainingData -= chunkSize;

        if (remainingData)
        {
            _fenceManager.signal(_fenceId, commandQueue);
            _fenceManager.waitForLastSignal(_fenceId);
        }
    }
}

void ResourceManager::updateResource(ID3D12GraphicsCommandList* cmdList,
                                     ID3D12CommandQueue* commandQueue, const std::string& name,
                                     void* data, uint64_t dataSize, uint32_t alignment,
                                     uint32_t frameIndex, bool isFrameResource,
                                     uint64_t destinationOffset)
{
    GPUView* gpuView;
    if (isFrameResource)
    {
        gpuView = &_frameResource[name][frameIndex];
    }
    else
    {
        gpuView = &_staticResources[name];
    }
    uploadToResource(cmdList, commandQueue, gpuView->_resource, data, dataSize, alignment,
                     frameIndex, destinationOffset);
}

GPUResource* ResourceManager::createBufferResource(uint64_t size,
                                                   D3D12_RESOURCE_STATES initialState,
                                                   D3D12_HEAP_TYPE heapType,
                                                   D3D12_HEAP_FLAGS heapFlags)
{
    auto& resource = _resources.emplace_back(std::make_unique<GPUResource>(initialState));
    CD3DX12_HEAP_PROPERTIES heapProps(heapType);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    HRESULT hr = _device->CreateCommittedResource(&heapProps, heapFlags, &bufferDesc, initialState,
                                                  nullptr, IID_PPV_ARGS(&resource->_resource));

    if (FAILED(hr))
    {
        _resources.pop_back();
        return nullptr;
    }

    resource->_init = true;
    return resource.get();
}

GPUResource* ResourceManager::createTexture2DResource(
    uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType, D3D12_HEAP_FLAGS heapFlags)
{
    auto& resource = _resources.emplace_back(std::make_unique<GPUResource>(initialState));

    CD3DX12_HEAP_PROPERTIES heapProps(heapType);
    CD3DX12_RESOURCE_DESC textureDesc =
        CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1, 1, 0, flags);

    D3D12_CLEAR_VALUE* clearValue = nullptr;
    D3D12_CLEAR_VALUE clearValueData = {};

    if (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
    {
        clearValueData.Format = format;
        clearValueData.Color[0] = 0.0f;
        clearValueData.Color[1] = 0.0f;
        clearValueData.Color[2] = 0.0f;
        clearValueData.Color[3] = 1.0f;
        clearValue = &clearValueData;
    }
    else if (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
    {
        clearValueData.Format = format;
        clearValueData.DepthStencil.Depth = 1.0f;
        clearValueData.DepthStencil.Stencil = 0;
        clearValue = &clearValueData;
    }

    HRESULT hr = _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                  initialState, clearValue,
                                                  IID_PPV_ARGS(&resource->_resource));

    if (FAILED(hr))
    {
        _resources.pop_back();
        return nullptr;
    }

    resource->_init = true;
    return resource.get();
}
}  // namespace rayvox