#include "ResourceManager.h"

#include <wrl/client.h>

#include <cstdint>

#include "AssertUtils.h"
#include "CommandQueue.h"
#include "FenceManager.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "DirectX-Headers/include/directx/d3d12.h"
#include "Handles.h"

namespace rayvox
{
ResourceManager::ResourceManager(const ComPtr<ID3D12Device2>& device, FenceManager& fenceManager,
                                 uint8_t frameCount, uint32_t uploadBufferSize)
    : _device(device), _frameCount(frameCount), _fenceManager(fenceManager)
{
    _descriptorHeapAllocator_CBV_SRV_UAV.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128);
    _descriptorHeapAllocator_SAMPLER.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 8);
    _descriptorHeapAllocator_RTV.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3);
    _descriptorHeapAllocator_DSV.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    for (int i = 0; i < frameCount; ++i)
    {
        auto& buffer = _uploadBuffers.emplace_back();

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

void ResourceManager::requestUpload(GPUHandle guid, const void* data, uint64_t dataSize,
                                    uint32_t alignement, uint64_t destinationOffset)
{
    _uploadRequests.push_back({guid, data, dataSize, alignement, destinationOffset});
}

void ResourceManager::flushUploadRequests(ID3D12GraphicsCommandList* cmdList,
                                          ID3D12CommandQueue* commandQueue, uint32_t frameIndex)
{
    for (auto& req : _uploadRequests)
    {
        updateResource(cmdList, commandQueue, req._guid, req._data, req._dataSize, req._alignement,
                       frameIndex, req._destinationOffset);
    }
    _uploadRequests.clear();
}

void ResourceManager::uploadToResource(ID3D12GraphicsCommandList* cmdList,
                                       ID3D12CommandQueue* commandQueue, GPUResource* destination,
                                       const void* data, uint64_t dataSize, uint32_t alignment,
                                       uint32_t frameIndex, uint64_t destinationOffset)
{
    auto& upload = _uploadBuffers[frameIndex];

    upload._currentOffset = AlignUp(upload._currentOffset, alignment);

    uint64_t remainingData = dataSize;
    uint64_t dataOffset = 0;

    while (remainingData)
    {
        uint64_t chunkSize = std::min(upload._size - upload._currentOffset, remainingData);

        memcpy(static_cast<uint8_t*>(upload._mappedData) + upload._currentOffset,
               static_cast<const uint8_t*>(data) + dataOffset, chunkSize);
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
                                     ID3D12CommandQueue* commandQueue, const std::string_view& name,
                                     const void* data, uint64_t dataSize, uint32_t alignment,
                                     uint32_t frameIndex, bool isFrameResource,
                                     uint64_t destinationOffset)
{
    GPUView* gpuView;
    if (isFrameResource)
    {
        gpuView = &_frameViews.at(_nameToGuidMap.at(name.data()))[frameIndex];
    }
    else
    {
        gpuView = &_staticViews.at(_nameToGuidMap.at(name.data()));
    }
    gpuView->_resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
    uploadToResource(cmdList, commandQueue, gpuView->_resource, data, dataSize, alignment,
                     frameIndex, destinationOffset);
}

void ResourceManager::updateResource(ID3D12GraphicsCommandList* cmdList,
                                     ID3D12CommandQueue* commandQueue, GPUHandle& guid,
                                     const void* data, uint64_t dataSize, uint32_t alignment,
                                     uint32_t frameIndex, uint64_t destinationOffset)
{
    GPUView* gpuView;
    if (guid._type == GPUHandle::ObjectType::FrameView)
    {
        gpuView = &_frameViews.at(guid)[frameIndex];
    }
    else
    {
        gpuView = &_staticViews.at(guid);
    }
    gpuView->_resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
    uploadToResource(cmdList, commandQueue, gpuView->_resource, data, dataSize, alignment,
                     frameIndex, destinationOffset);
}

GPUHandle ResourceManager::createEmptyStaticResource(std::optional<std::string_view> name)
{
    GPUHandle guid = generateGUID(GPUHandle::ObjectType::StaticResource, name);
    _staticResources.emplace(guid, std::make_unique<GPUResource>(D3D12_RESOURCE_STATE_COMMON));
    return guid;
}

GPUHandle ResourceManager::createEmptyFrameResource(std::optional<std::string_view> name)
{
    GPUHandle guid = generateGUID(GPUHandle::ObjectType::FrameResource, name);
    _frameResource[guid] = std::vector<std::unique_ptr<GPUResource>>();
    for (uint8_t i = 0; i < _frameCount; ++i)
    {
        auto res = std::make_unique<GPUResource>(D3D12_RESOURCE_STATE_COMMON);
        _frameResource.at(guid).push_back(std::move(res));
    }
    return guid;
}

GPUHandle ResourceManager::createBufferStaticResource(uint64_t size,
                                                      D3D12_RESOURCE_STATES initialState,
                                                      D3D12_HEAP_TYPE heapType,
                                                      std::optional<std::string_view> name,
                                                      D3D12_HEAP_FLAGS heapFlags)
{
    GPUHandle guid = generateGUID(GPUHandle::ObjectType::StaticResource, name);

    _staticResources.emplace(guid, std::make_unique<GPUResource>(initialState));
    auto res = _staticResources.at(guid).get();

    CD3DX12_HEAP_PROPERTIES heapProps(heapType);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    HRESULT hr = _device->CreateCommittedResource(&heapProps, heapFlags, &bufferDesc, initialState,
                                                  nullptr, IID_PPV_ARGS(&res->_resource));

    ThrowIfFailed(hr);

    if (name)
    {
        std::wstring wname(name->begin(), name->end());
        res->_resource->SetName(wname.c_str());
        res->_init = true;
    }
    return guid;
}

GPUHandle ResourceManager::createBufferFrameResource(uint64_t size,
                                                     D3D12_RESOURCE_STATES initialState,
                                                     D3D12_HEAP_TYPE heapType,
                                                     std::optional<std::string_view> name,
                                                     D3D12_HEAP_FLAGS heapFlags)
{
    GPUHandle guid = generateGUID(GPUHandle::ObjectType::FrameResource, name);

    auto& resources = _frameResource[guid];
    resources.reserve(_frameCount);

    for (uint8_t i = 0; i < _frameCount; ++i)
    {
        auto res = resources.emplace_back(std::make_unique<GPUResource>(initialState)).get();

        CD3DX12_HEAP_PROPERTIES heapProps(heapType);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

        HRESULT hr = _device->CreateCommittedResource(&heapProps, heapFlags, &bufferDesc,
                                                      initialState, nullptr,
                                                      IID_PPV_ARGS(&res->_resource));

        ThrowIfFailed(hr);

        if (name)
        {
            std::wstring wname(name->begin(), name->end());
            res->_resource->SetName(wname.c_str());
            res->_init = true;
        }
    }

    return guid;
}

GPUHandle ResourceManager::createTexture2DStaticResource(
    uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType,
    std::optional<std::string_view> name, D3D12_HEAP_FLAGS heapFlags)
{
    GPUHandle guid = generateGUID(GPUHandle::ObjectType::StaticResource, name);

    _staticResources.emplace(guid, std::make_unique<GPUResource>(initialState));
    auto resource = _staticResources.at(guid).get();

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

    ThrowIfFailed(hr);

    if (name)
    {
        std::wstring wname(name->begin(), name->end());
        resource->_resource->SetName(wname.c_str());
        resource->_init = true;
    }
    return guid;
}

GPUHandle ResourceManager::createTexture2DFrameResource(
    uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType,
    std::optional<std::string_view> name, D3D12_HEAP_FLAGS heapFlags)
{
    GPUHandle guid = generateGUID(GPUHandle::ObjectType::FrameResource, name);

    auto& resources = _frameResource[guid];
    resources.reserve(_frameCount);

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

    for (uint8_t i = 0; i < _frameCount; ++i)
    {
        auto resource = resources.emplace_back(std::make_unique<GPUResource>(initialState)).get();
        HRESULT hr = _device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                      &textureDesc, initialState, clearValue,
                                                      IID_PPV_ARGS(&resource->_resource));

        ThrowIfFailed(hr);

        if (name)
        {
            std::wstring wname(name->begin(), name->end());
            resource->_resource->SetName(wname.c_str());
            resource->_init = true;
        }
    }

    return guid;
}

void ResourceManager::destroyResource(GPUHandle guid)
{
    // TODO
}

GPUResource* ResourceManager::getStaticResource(RN n)
{
    auto itGuid = _nameToGuidMap.find(toS(n));
    ThrowAssert(itGuid != _nameToGuidMap.end(), "guid name " + toS(n) + " not found.");

    auto itRes = _staticResources.find(itGuid->second);
    ThrowAssert(itRes != _staticResources.end(),
                "resource " + itGuid->second.toString() + " not found.");
    return itRes->second.get();
}

std::vector<GPUResource*> ResourceManager::getFrameResource(RN n)
{
    auto itGuid = _nameToGuidMap.find(toS(n));
    ThrowAssert(itGuid != _nameToGuidMap.end(), "guid name " + toS(n) + " not found.");

    auto itRes = _frameResource.find(itGuid->second);
    ThrowAssert(itRes != _frameResource.end(),
                "resource " + itGuid->second.toString() + " not found.");

    std::vector<GPUResource*> result;
    for (auto& res : itRes->second)
        result.push_back(res.get());
    return result;
}

GPUView& ResourceManager::getStaticView(RN n)
{
    auto itGuid = _nameToGuidMap.find(toS(n));
    ThrowAssert(itGuid != _nameToGuidMap.end(), "guid name " + toS(n) + " not found.");

    auto itView = _staticViews.find(itGuid->second);
    ThrowAssert(itView != _staticViews.end(), "view " + itGuid->second.toString() + " not found.");

    return itView->second;
}

std::vector<GPUView>& ResourceManager::getFrameView(RN n)
{
    auto itGuid = _nameToGuidMap.find(toS(n));
    ThrowAssert(itGuid != _nameToGuidMap.end(), "guid name " + toS(n) + " not found.");

    auto itView = _frameViews.find(itGuid->second);
    ThrowAssert(itView != _frameViews.end(), "view " + itGuid->second.toString() + " not found.");

    return itView->second;
}

GPUResource* ResourceManager::getStaticResource(GPUHandle& guid)
{
    auto itRes = _staticResources.find(guid);
    ThrowAssert(itRes != _staticResources.end(), "resource " + guid.toString() + " not found.");
    return itRes->second.get();
}

std::vector<GPUResource*> ResourceManager::getFrameResource(GPUHandle& guid)
{
    auto itRes = _frameResource.find(guid);
    ThrowAssert(itRes != _frameResource.end(), "resource " + guid.toString() + " not found.");

    std::vector<GPUResource*> result;
    for (auto& res : itRes->second)
        result.push_back(res.get());
    return result;
}

GPUView& ResourceManager::getStaticView(GPUHandle& guid)
{
    auto itView = _staticViews.find(guid);
    ThrowAssert(itView != _staticViews.end(), "view " + guid.toString() + " not found.");
    return itView->second;
}

std::vector<GPUView>& ResourceManager::getFrameView(GPUHandle& guid)
{
    auto itView = _frameViews.find(guid);
    ThrowAssert(itView != _frameViews.end(), "view " + guid.toString() + " not found.");
    return itView->second;
}

GPUHandle ResourceManager::generateGUID(GPUHandle::ObjectType type,
                                        std::optional<std::string_view> name)
{
    if (name.has_value())
    {
        auto guid = GPUHandle(type, name.value().data());
        ThrowAssert(!_frameViews.contains(guid), "Resource name already exists");
        _nameToGuidMap[name->data()] = guid;
        _createdGPUHandle.insert(guid);
        return guid;
    }
    else
    {
        auto guid = GPUHandle(type);
        while (_createdGPUHandle.contains(guid))
        {
            guid = GPUHandle(type);
        }
        _createdGPUHandle.insert(guid);
        return guid;
    }
}
}  // namespace rayvox