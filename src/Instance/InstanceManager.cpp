#include "InstanceManager.h"
#include <cstddef>
#include <cstdint>
#include "Components/ComponentFlag.h"
#include "Components/EntityHandle.h"
#include "Components/RenderInstanceID_C.h"
#include "InstanceKind.h"
#include "Renderer/ResourceManager.h"
#include "instanceDeclaration.h"

namespace rayvox
{
GPUInstanceManager::GPUInstanceManager(ResourceManager& rm) : _resourceManager(rm) {};

void GPUInstanceManager::uploadRemainingFrameDirty(uint8_t frameIndex)
{
    auto upload = [&](auto& frameInstancePool)
    {
        TempBytes<256> tmp;
        auto& map = frameInstancePool._dirtyComponents;
        for (auto it = map.begin(); it != map.end();)
        {
            const EntityHandle& entityHandle = it->first;
            FrameDirtyFlag& frameDirtyFlag = it->second;
            uint32_t dirtyComponentsFlag =
                static_cast<uint32_t>(frameDirtyFlag._dirtyComponentsByFrame[frameIndex]);
            if (dirtyComponentsFlag == 0)
                continue;

            auto* instance = frameInstancePool.getOrNull(entityHandle);
            if (!instance)
            {
                it = map.erase(it);
                continue;
            }

            while (dirtyComponentsFlag)
            {
                size_t bitIndex = static_cast<size_t>(std::countr_zero(dirtyComponentsFlag));
                dirtyComponentsFlag &= (dirtyComponentsFlag - 1u);  // set lsb 1 to 0

                auto patchRange = InstancePatches<StaticMeshInstance>::byBit[bitIndex];

                for (const PatchDesc& p : patchRange.patches)
                {
                    std::byte* buf = tmp.get(p._size);
                    p.fill(*entityHandle._reg, entityHandle._entity, buf);

                    const uint32_t stride = sizeof(StaticMeshInstance::GPUData);
                    const uint32_t byteOffset = instance->_gpuIndex * stride + p._offset;

                    auto span = _resourceManager.requestUploadOwned(
                        frameInstancePool._instancePoolViewHandle, p._size, 256);
                    std::memcpy(span.data(), buf, p._size);
                }
            }
            frameDirtyFlag._dirtyComponentsByFrame[frameIndex] = ComponentFlag::None;

            if (frameDirtyFlag.none())
            {
                it = map.erase(it);
                continue;
            }
            ++it;
        }
    };

    upload(_meshInstancesPool);
}

void GPUInstanceManager::markDirty(const EntityHandle& handle, ComponentFlag componentFlag)
{
    auto* rID = handle._reg->try_get<RenderInstance_C>(handle._entity);
    if (!rID)
        return;

    switch (rID->_kind)
    {
        case InstanceKind::StaticMesh:
            _meshInstancesPool._dirtyComponents[handle].setAll(componentFlag);
            break;
        case InstanceKind::Camera:
            _cameraInstancesPool._dirtyComponents[handle].setAll(componentFlag);
            break;
    }
}
}  // namespace rayvox
