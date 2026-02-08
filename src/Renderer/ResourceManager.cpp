#include "ResourceManager.h"

#include <wrl/client.h>
#include "EngineConfig.h"
#include "Renderer/includeDX12.h"

#include "CommandQueue.h"
#include "DebugUtils.h"
#include "FenceManager.h"
#include "Handles.h"
#include "ResourceFormatWrapper.h"
#include "VariantUtils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#pragma clang optimize off
namespace rayvox
{
ResourceManager::ResourceManager(const Microsoft::WRL::ComPtr<ID3D12Device2>& device,
                                 FenceManager& fenceManager, uint32_t uploadBufferSize)
    : _device(device), _fenceManager(fenceManager)
{
    _descriptorHeapAllocator_CBV_SRV_UAV.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                              DescriptorHeapAllocator_CBV_SRV_UAV_size);
    _descriptorHeapAllocator_SAMPLER.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                          DescriptorHeapAllocator_Sampler_size);
    _descriptorHeapAllocator_RTV.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                      DescriptorHeapAllocator_RTV_size);
    _descriptorHeapAllocator_DSV.init(_device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                      DescriptorHeapAllocator_DSV_size);

    for (size_t i = 0; i < FramesInFlight; ++i)
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
    auto& req = _uploadRequests.emplace_back();
    req._guid = guid;
    req._data = data;
    req._dataSize = dataSize;
    req._alignment = alignement;
    req._destinationOffset = destinationOffset;
}

[nodiscard("You must handle the returned GPU upload span")] std::span<std::byte>
ResourceManager::requestUploadOwned(GPUHandle guid, uint64_t dataSize, uint32_t alignment,
                                    uint64_t destinationOffset)
{
    auto& req = _uploadRequests.emplace_back();
    req._guid = guid;
    req._alignment = alignment;
    req._destinationOffset = destinationOffset;
    req._ownedData.resize(dataSize);
    return std::span<std::byte>(req._ownedData);
}

void ResourceManager::flushUploadRequests(ID3D12GraphicsCommandList* cmdList,
                                          ID3D12CommandQueue* commandQueue, uint32_t frameIndex)
{
    for (auto& req : _uploadRequests)
    {
        updateResource(cmdList, commandQueue, req._guid, req.dataPtr(), req.size(), req._alignment,
                       frameIndex, req._destinationOffset);
    }
    _uploadRequests.clear();
    _uploadBuffers[frameIndex]._currentOffset = 0;  // reset upload buffer
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
        memcpy(static_cast<uint8_t*>(upload._mappedData) + upload._currentOffset,
               static_cast<const uint8_t*>(data) + dataOffset, chunkSize);
        cmdList->CopyBufferRegion(destination->get(), destinationOffset, upload._buffer.Get(),
                                  upload._currentOffset, chunkSize);
#pragma clang diagnostic pop

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
            std::cout << "FIXME : remainingData -> wait : repenser l'upload des cbv instanceData\n";
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
        gpuView = &_frameViews.at(_nameToViewGuidMap.at(name.data()))[frameIndex];
    }
    else
    {
        gpuView = &_staticViews.at(_nameToViewGuidMap.at(name.data()));
    }
    gpuView->_resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
    uploadToResource(cmdList, commandQueue, gpuView->_resource, data, dataSize, alignment,
                     frameIndex, destinationOffset);
}

void ResourceManager::updateResource(ID3D12GraphicsCommandList* cmdList,
                                     ID3D12CommandQueue* commandQueue, GPUHandle& handle,
                                     const void* data, uint64_t dataSize, uint32_t alignment,
                                     uint32_t frameIndex, uint64_t destinationOffset)
{
    GPUResource* resource = nullptr;

    std::visit(overloaded{[&](GPUResourceHandle& h)
                          {
                              if (h._type == GPUResourceHandle::ObjectType::FrameResource)
                              {
                                  resource = _frameResource.at(h)[frameIndex].get();
                              }
                              else
                              {
                                  resource = _staticResources.at(h).get();
                              }
                          },
                          [&](GPUViewHandle& h)
                          {
                              if (h._type == GPUViewHandle::ObjectType::FrameView)
                              {
                                  resource = _frameViews.at(h)[frameIndex]._resource;
                              }
                              else
                              {
                                  resource = _staticViews.at(h)._resource;
                              }
                          },
                          [&](GPUMeshViewHandle& h)
                          {
                              if (h._type == GPUMeshViewHandle::ObjectType::FrameMeshView)
                              {
                                  // todo
                              }
                              else
                              {
                                  resource = _staticMeshViews.at(h)._resource;
                              }
                          }},
               handle);

    if (!resource)
        return;

    resource->transitionTo(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
    uploadToResource(cmdList, commandQueue, resource, data, dataSize, alignment, frameIndex,
                     destinationOffset);
}

GPUResourceHandle ResourceManager::createEmptyStaticResource(std::optional<std::string_view> name)
{
    GPUResourceHandle guid = generateGUID(GPUResourceHandle::ObjectType::StaticResource, name);
    _staticResources.emplace(guid, std::make_unique<GPUResource>(D3D12_RESOURCE_STATE_COMMON));
    return guid;
}

GPUResourceHandle ResourceManager::createEmptyFrameResource(std::optional<std::string_view> name)
{
    GPUResourceHandle guid = generateGUID(GPUResourceHandle::ObjectType::FrameResource, name);
    _frameResource[guid] = std::vector<std::unique_ptr<GPUResource>>();
    for (uint8_t i = 0; i < FramesInFlight; ++i)
    {
        auto res = std::make_unique<GPUResource>(D3D12_RESOURCE_STATE_COMMON);
        _frameResource.at(guid).push_back(std::move(res));
    }
    return guid;
}

GPUResourceHandle ResourceManager::createBufferStaticResource(uint64_t size,
                                                              D3D12_RESOURCE_STATES initialState,
                                                              D3D12_HEAP_TYPE heapType,
                                                              std::optional<std::string_view> name,
                                                              D3D12_HEAP_FLAGS heapFlags)
{
    GPUResourceHandle guid = generateGUID(GPUResourceHandle::ObjectType::StaticResource, name);

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

GPUResourceHandle ResourceManager::createBufferFrameResource(uint64_t size,
                                                             D3D12_RESOURCE_STATES initialState,
                                                             D3D12_HEAP_TYPE heapType,
                                                             std::optional<std::string_view> name,
                                                             D3D12_HEAP_FLAGS heapFlags)
{
    GPUResourceHandle guid = generateGUID(GPUResourceHandle::ObjectType::FrameResource, name);

    auto& resources = _frameResource[guid];
    resources.reserve(FramesInFlight);

    for (uint8_t i = 0; i < FramesInFlight; ++i)
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

GPUResourceHandle ResourceManager::createTexture2DStaticResource(
    uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType,
    std::optional<std::string_view> name, D3D12_HEAP_FLAGS heapFlags)
{
    GPUResourceHandle guid = generateGUID(GPUResourceHandle::ObjectType::StaticResource, name);

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

GPUResourceHandle ResourceManager::createTexture2DFrameResource(
    uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType,
    std::optional<std::string_view> name, D3D12_HEAP_FLAGS heapFlags)
{
    GPUResourceHandle guid = generateGUID(GPUResourceHandle::ObjectType::FrameResource, name);

    auto& resources = _frameResource[guid];
    resources.reserve(FramesInFlight);

    CD3DX12_HEAP_PROPERTIES heapProps(heapType);
    CD3DX12_RESOURCE_DESC textureDesc =
        CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1, 1, 0, flags);

    D3D12_CLEAR_VALUE* clearValue = nullptr;
    D3D12_CLEAR_VALUE clearValueData = {};

    if (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
    {
        clearValueData.Format = format;
        clearValueData.Color[0] = 0.2f;
        clearValueData.Color[1] = 0.2f;
        clearValueData.Color[2] = 0.2f;
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

    for (uint8_t i = 0; i < FramesInFlight; ++i)
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

GPUViewHandle ResourceManager::createStaticCBV(GPUResourceHandle resource_guid,
                                               std::optional<std::string_view> name,
                                               uint64_t offset, uint64_t size)
{
    auto* resource = getStaticResource(resource_guid);
    const auto& descRes = resource->_resource->GetDesc();

    D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
    desc.BufferLocation = resource->_resource->GetGPUVirtualAddress() + offset;
    desc.SizeInBytes = static_cast<UINT>(AlignUp(size > 0 ? size : descRes.Width, 256));

    auto guid = createStaticView(resource_guid, desc, name);

    return guid;
}

GPUViewHandle ResourceManager::createFrameCBV(GPUResourceHandle resource_guid,
                                              std::optional<std::string_view> name, uint64_t offset,
                                              uint64_t size)
{
    auto resources = getFrameResource(resource_guid);

    std::vector<D3D12_CONSTANT_BUFFER_VIEW_DESC> descs;
    descs.reserve(FramesInFlight);

    for (auto resource : resources)
    {
        const auto& descRes = resource->get()->GetDesc();

        D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
        desc.BufferLocation = resource->get()->GetGPUVirtualAddress() + offset;
        desc.SizeInBytes = static_cast<UINT>(AlignUp(size > 0 ? size : descRes.Width, 256));
        descs.push_back(desc);
    }

    return createFrameView<D3D12_CONSTANT_BUFFER_VIEW_DESC>(resource_guid, descs, name);
}

GPUMeshViewHandle ResourceManager::createStaticIBV(GPUResourceHandle resource_guid,
                                                   ResourceFormat format,
                                                   std::optional<std::string_view> name,
                                                   uint64_t offset, uint64_t size)
{
    auto* resource = getStaticResource(resource_guid);
    ThrowAssert(resource && resource->get(), "createStaticIBV: resource not created");

    const auto descRes = resource->get()->GetDesc();
    ThrowAssert(descRes.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER,
                "createStaticIBV: resource is not a buffer");

    const uint64_t totalSize = descRes.Width;

    ThrowAssert(offset <= totalSize, "createStaticIBV: offset out of bounds");
    if (size == 0)
        size = totalSize - offset;
    ThrowAssert(offset + size <= totalSize, "createStaticIBV: (offset+size) out of bounds");

    // Alignment check (R16 -> 2, R32 -> 4)
    const uint64_t align = (format == ResourceFormat::R16_UINT) ? 2ull : 4ull;
    ThrowAssert((offset % align) == 0, "createStaticIBV: offset misaligned for index format");
    ThrowAssert((size % align) == 0, "createStaticIBV: size misaligned for index format");

    GPUMeshViewHandle guid = generateGUID(GPUMeshViewHandle::ObjectType::StaticMeshView, name);

    GPUMeshView view{};
    view._resource = resource;
    view._resourceHandle = resource_guid;
    view._type = GPUMeshView::Type::Index;

    view.ibv.BufferLocation = resource->get()->GetGPUVirtualAddress() + offset;
    view.ibv.SizeInBytes = static_cast<UINT>(size);
    view.ibv.Format = static_cast<DXGI_FORMAT>(format);

    _staticMeshViews[guid] = view;
    return guid;
}

GPUMeshViewHandle ResourceManager::createStaticVBV(GPUResourceHandle resource_guid,
                                                   uint32_t strideBytes,
                                                   std::optional<std::string_view> name,
                                                   uint64_t offset, uint64_t size)
{
    auto* resource = getStaticResource(resource_guid);
    ThrowAssert(resource && resource->get(), "createStaticVBV: resource not created");

    const auto descRes = resource->get()->GetDesc();
    ThrowAssert(descRes.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER,
                "createStaticVBV: resource is not a buffer");

    const uint64_t totalSize = descRes.Width;

    ThrowAssert(offset <= totalSize, "createStaticVBV: offset out of bounds");
    if (size == 0)
        size = totalSize - offset;
    ThrowAssert(offset + size <= totalSize, "createStaticVBV: (offset+size) out of bounds");

    GPUMeshViewHandle guid = generateGUID(GPUMeshViewHandle::ObjectType::StaticMeshView, name);

    GPUMeshView view{};
    view._resource = resource;
    view._resourceHandle = resource_guid;
    view._type = GPUMeshView::Type::Vertex;

    view.vbv.BufferLocation = resource->get()->GetGPUVirtualAddress() + offset;
    view.vbv.SizeInBytes = static_cast<UINT>(size);
    view.vbv.StrideInBytes = strideBytes;

    _staticMeshViews[guid] = view;
    return guid;
}

void ResourceManager::releaseViewDescriptor(GPUView& v)
{
    if (!v._descriptorHandle)
        return;

    switch (v._type)
    {
        case GPUView::Type::CBV:
        case GPUView::Type::SRV:
        case GPUView::Type::UAV:
            _descriptorHeapAllocator_CBV_SRV_UAV.free(*v._descriptorHandle);
            break;
        case GPUView::Type::RTV:
            _descriptorHeapAllocator_RTV.free(*v._descriptorHandle);
            break;
        case GPUView::Type::DSV:
            _descriptorHeapAllocator_DSV.free(*v._descriptorHandle);
            break;
    }

    v._descriptorHandle = nullptr;
}

void ResourceManager::requestDestroy(GPUResourceHandle guid)
{
    _aliveGPUResourceHandle.erase(guid);

    if (guid._type == GPUResourceHandle::ObjectType::StaticResource)
    {
        auto it = _staticResources.find(guid);
        if (it == _staticResources.end())
            return;

        _deferredReleases.push_back(
            DeferredRelease{.fenceValue = 0, .object = GPUObject{std::move(*it->second)}});

        _staticResources.erase(it);
        return;
    }

    if (guid._type == GPUResourceHandle::ObjectType::FrameResource)
    {
        auto it = _frameResource.find(guid);
        if (it == _frameResource.end())
            return;

        for (auto& uptr : it->second)
        {
            if (!uptr)
                continue;

            _deferredReleases.push_back(
                DeferredRelease{.fenceValue = 0, .object = GPUObject{std::move(*uptr)}});
        }

        _frameResource.erase(it);
        return;
    }
}

void ResourceManager::requestDestroy(GPUViewHandle guid, bool destroyAssociatedResources)
{
    _aliveGPUViewHandle.erase(guid);

    if (guid._type == GPUViewHandle::ObjectType::StaticView)
    {
        auto it = _staticViews.find(guid);
        if (it == _staticViews.end())
            return;

        if (destroyAssociatedResources)
            requestDestroy(it->second._resourceHandle);

        _deferredReleases.push_back(
            DeferredRelease{.fenceValue = 0, .object = GPUObject{std::move(it->second)}});

        _staticViews.erase(it);
        return;
    }

    if (guid._type == GPUViewHandle::ObjectType::FrameView)
    {
        auto it = _frameViews.find(guid);
        if (it == _frameViews.end())
            return;

        if (destroyAssociatedResources && !it->second.empty())
        {
            requestDestroy(
                it->second[0]
                    ._resourceHandle);  // only one request because all views of FrameViews have the same ResourceHandle (FrameResourceHandle)
        }

        for (auto& v : it->second)
        {
            _deferredReleases.push_back(
                DeferredRelease{.fenceValue = 0, .object = GPUObject{std::move(v)}});
        }

        _frameViews.erase(it);
        return;
    }
}

void ResourceManager::requestDestroy(GPUMeshViewHandle guid, bool destroyAssociatedResources)
{
    _aliveGPUMeshViewHandle.erase(guid);

    auto it = _staticMeshViews.find(guid);
    if (it == _staticMeshViews.end())
        return;

    if (destroyAssociatedResources)
        requestDestroy(it->second._resourceHandle);

    _deferredReleases.push_back(
        DeferredRelease{.fenceValue = 0, .object = GPUObject{std::move(it->second)}});

    _staticMeshViews.erase(it);
}

void ResourceManager::flushDeferredReleases(ID3D12CommandQueue* commandQueue)
{
    if(_deferredReleases.empty()) return;
    
    // 1: signal if resource are used by gpu
    bool hasPending = false;
    for (const auto& e : _deferredReleases)
    {
        if (e.fenceValue == 0) { hasPending = true; break; }
    }

    uint64_t stampedValue = 0;
    if (hasPending)
        stampedValue = _fenceManager.signal(_fenceId, commandQueue);

    if (stampedValue != 0)
    {
        for (auto& e : _deferredReleases)
        {
            if (e.fenceValue == 0)
                e.fenceValue = stampedValue;
        }
    }

    // 2: release not stamped resources
    size_t write = 0;
    for (size_t read = 0; read < _deferredReleases.size(); ++read)
    {
        auto& entry = _deferredReleases[read];

        if (!_fenceManager.isFenceComplete(_fenceId, entry.fenceValue))
        {
            _deferredReleases[write++] = std::move(entry);
            continue;
        }

        std::visit([this](auto& obj)
        {
            using T = std::decay_t<decltype(obj)>;
            if constexpr (std::is_same_v<T, GPUView>)
            {
                releaseViewDescriptor(obj);
            }
        }, entry.object);
    }

    _deferredReleases.erase(_deferredReleases.begin() + static_cast<long long>(write), _deferredReleases.end());
}

GPUResource* ResourceManager::getStaticResource(RN n)
{
    auto itGuid = _nameToResourceGuidMap.find(toS(n));
    ThrowAssert(itGuid != _nameToResourceGuidMap.end(), "guid name " + toS(n) + " not found.");

    return getStaticResource(itGuid->second);
}

GPUResource* ResourceManager::getStaticResource(GPUResourceHandle& guid)
{
    auto itRes = _staticResources.find(guid);
    ThrowAssert(itRes != _staticResources.end(), "resource " + guid.toString() + " not found.");
    return itRes->second.get();
}

std::vector<GPUResource*> ResourceManager::getFrameResource(RN n)
{
    auto itGuid = _nameToResourceGuidMap.find(toS(n));
    ThrowAssert(itGuid != _nameToResourceGuidMap.end(), "guid name " + toS(n) + " not found.");

    return getFrameResource(itGuid->second);
}

std::vector<GPUResource*> ResourceManager::getFrameResource(GPUResourceHandle& guid)
{
    auto itRes = _frameResource.find(guid);
    ThrowAssert(itRes != _frameResource.end(), "resource " + guid.toString() + " not found.");

    std::vector<GPUResource*> result;
    for (auto& res : itRes->second)
        result.push_back(res.get());
    return result;
}

GPUView& ResourceManager::getStaticView(RN n)
{
    auto itGuid = _nameToViewGuidMap.find(toS(n));
    ThrowAssert(itGuid != _nameToViewGuidMap.end(), "guid name " + toS(n) + " not found.");

    return getStaticView(itGuid->second);
}

GPUView& ResourceManager::getStaticView(GPUViewHandle& guid)
{
    auto itView = _staticViews.find(guid);
    ThrowAssert(itView != _staticViews.end(), "view " + guid.toString() + " not found.");
    return itView->second;
}

std::vector<GPUView>& ResourceManager::getFrameView(RN n)
{
    auto itGuid = _nameToViewGuidMap.find(toS(n));
    ThrowAssert(itGuid != _nameToViewGuidMap.end(), "guid name " + toS(n) + " not found.");

    return getFrameView(itGuid->second);
}

std::vector<GPUView>& ResourceManager::getFrameView(GPUViewHandle& guid)
{
    auto itView = _frameViews.find(guid);
    ThrowAssert(itView != _frameViews.end(), "view " + guid.toString() + " not found.");
    return itView->second;
}

GPUMeshView& ResourceManager::getStaticMeshView(RN n)
{
    auto itGuid = _nameToMeshViewGuidMap.find(toS(n));
    ThrowAssert(itGuid != _nameToMeshViewGuidMap.end(), "guid name " + toS(n) + " not found.");

    return getStaticMeshView(itGuid->second);
}

GPUMeshView& ResourceManager::getStaticMeshView(GPUMeshViewHandle& guid)
{
    auto itView = _staticMeshViews.find(guid);
    ThrowAssert(itView != _staticMeshViews.end(), "view " + guid.toString() + " not found.");
    return itView->second;
}

GPUResourceHandle ResourceManager::generateGUID(GPUResourceHandle::ObjectType type,
                                                std::optional<std::string_view> name)
{
    if (name.has_value())
    {
        auto guid = GPUResourceHandle(type, name.value().data());
        ThrowAssert(!_aliveGPUResourceHandle.contains(guid), "Resource name already exists");
        _nameToResourceGuidMap[name->data()] = guid;
        _aliveGPUResourceHandle.insert(guid);
        return guid;
    }
    else
    {
        auto guid = GPUResourceHandle(type);
        while (_aliveGPUResourceHandle.contains(guid))
        {
            guid = GPUResourceHandle(type);
        }
        _aliveGPUResourceHandle.insert(guid);
        return guid;
    }
}

GPUViewHandle ResourceManager::generateGUID(GPUViewHandle::ObjectType type,
                                            std::optional<std::string_view> name)
{
    if (name.has_value())
    {
        auto guid = GPUViewHandle(type, name.value().data());
        ThrowAssert(!_aliveGPUViewHandle.contains(guid), "View name already exists");
        _nameToViewGuidMap[name->data()] = guid;
        _aliveGPUViewHandle.insert(guid);
        return guid;
    }
    else
    {
        auto guid = GPUViewHandle(type);
        while (_aliveGPUViewHandle.contains(guid))
        {
            guid = GPUViewHandle(type);
        }
        _aliveGPUViewHandle.insert(guid);
        return guid;
    }
}
GPUMeshViewHandle ResourceManager::generateGUID(GPUMeshViewHandle::ObjectType type,
                                                std::optional<std::string_view> name)
{
    if (name.has_value())
    {
        auto guid = GPUMeshViewHandle(type, name.value().data());
        ThrowAssert(!_aliveGPUMeshViewHandle.contains(guid), "View name already exists");
        _nameToMeshViewGuidMap[name->data()] = guid;
        _aliveGPUMeshViewHandle.insert(guid);
        return guid;
    }
    else
    {
        auto guid = GPUMeshViewHandle(type);
        while (_aliveGPUMeshViewHandle.contains(guid))
        {
            guid = GPUMeshViewHandle(type);
        }
        _aliveGPUMeshViewHandle.insert(guid);
        return guid;
    }
}
}  // namespace rayvox
