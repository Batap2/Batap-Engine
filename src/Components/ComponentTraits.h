#pragma once
#include "Camera_C.h"
#include "Renderer/ResourceManager.h"
#include "Transform_C.h"

template <typename T>
struct ComponentTraits;  // déclaration générique

using namespace rayvox;

// ---- Camera ----
template <>
struct ComponentTraits<Camera_C>
{
    static GPU_GUID create(ResourceManager& r, const Camera_C&)
    {
        // ici je peux soit faire un static buffer soit un frame buffer (1 par in flight frame)
        r.createStaticCBV(GPU_GUID resource_guid, DescriptorHeapAllocator & allocator);
        r.createFrameCBV(GPU_GUID resource_guid, DescriptorHeapAllocator & allocator);
    }
    static void destroy(ResourceManager& r, GPU_GUID id)
    {
        r.destroyResource(id);
    }
    static void upload(ResourceManager& r, GPU_GUID id, const Camera_C& c, uint32_t frameIndex)
    {
        r.updateResource(ID3D12GraphicsCommandList * cmdList, ID3D12CommandQueue * commandQueue,
                         GPU_GUID & guid, const void* data, uint64_t dataSize, uint32_t alignment,
                         uint32_t frameIndex, uint64_t destinationOffset = 0);
    }
};

// ---- Transform ----
template <>
struct ComponentTraits<Transform_C>
{
    static GPU_GUID create(ResourceManager& r, const Transform_C&)
    {
        return r.createConstantBuffer(sizeof(Transform_C));
    }
    static void destroy(ResourceManager& r, GPU_GUID id)
    {
        r.destroyResource(id);
    }
    static void upload(ResourceManager& r, GPU_GUID id, const Transform_C& t, uint32_t frameIndex)
    {
        r.uploadConstant(id, frameIndex, &t, sizeof(Transform_C));
    }
};
