#pragma once
#include <optional>
#include "Camera_C.h"
#include "DirectX-Headers/include/directx/d3d12.h"
#include "Renderer/GPU_GUID.h"
#include "Renderer/ResourceManager.h"
#include "Transform_C.h"

namespace rayvox
{
template <typename T>
struct ComponentTraits
{
    static void destroy(ResourceManager& r, const T& c)
    {
    }
    static void upload(ResourceManager&, T&, uint32_t)
    {
    }
};

template <>
struct ComponentTraits<Camera_C>
{
    static void destroy(ResourceManager& r, const Camera_C& c)
    {
        r.destroyResource(c._buffer_ID);
    }
    static void upload(ResourceManager& r, Camera_C& c, uint32_t frameIndex)
    {
        if (!c._buffer_ID._guid)
        {
            auto resourceGuid =
                r.createBufferFrameResource(ResourceManager::AlignUp(sizeof(Camera_C::Data), 256),
                                            D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE_DEFAULT);
            c._buffer_ID = r.createFrameCBV(resourceGuid, std::nullopt, 0, sizeof(Camera_C::Data));
        }

        if (!c._dirty.isDirty(frameIndex))
            return;

        auto data = c.dataView();
        r.requestUpload(c._buffer_ID, data.data(), data.size(), 256);

        c._dirty.clear(frameIndex);
    }
};
}  // namespace rayvox
