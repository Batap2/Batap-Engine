#include "InstanceManager.h"
#include <cstddef>
#include <cstdint>
#include "Components/ComponentFlag.h"
#include "Components/EntityHandle.h"
#include "Components/RenderInstanceID_C.h"
#include "InstanceKind.h"
#include "Renderer/Renderer.h"
#include "Renderer/ResourceManager.h"
#include "instanceDeclaration.h"

namespace batap
{
GPUInstanceManager::GPUInstanceManager(Context& ctx)
    : _resourceManager(*ctx._renderer->_resourceManager) {};

void GPUInstanceManager::uploadRemainingFrameDirty(Context& ctx)
{
    auto frameIndex = ctx.getFrameindex();
    auto upload = [&](auto& frameInstancePool)
    {
        using PoolT = std::remove_reference_t<decltype(frameInstancePool)>;
        using InstanceT = typename PoolT::InstanceType;

        TempBytes<256> tmp;
        auto& map = frameInstancePool.dirtyComponents_;
        for (auto it = map.begin(); it != map.end();)
        {
            const EntityHandle& entityHandle = it->first;
            FrameDirtyFlag& frameDirtyFlag = it->second;
            uint32_t dirtyComponentsFlag =
                static_cast<uint32_t>(frameDirtyFlag._dirtyComponentsByFrame[frameIndex]);
            if (dirtyComponentsFlag == 0)
            {
                ++it;
                continue;
            }

            GPUInstanceID id = frameInstancePool.getGPUIndex(entityHandle);
            if (!id.valid())
            {
                it = map.erase(it);
                continue;
            }

            while (dirtyComponentsFlag)
            {
                size_t bitIndex = static_cast<size_t>(std::countr_zero(dirtyComponentsFlag));
                dirtyComponentsFlag &= (dirtyComponentsFlag - 1u);  // set lsb 1 to 0

                auto patchRange = InstancePatches<InstanceT>::byBit[bitIndex];

                for (const PatchDesc& p : patchRange.patches)
                {
                    auto buf = tmp.get(sizeof(typename InstanceT::GPUData));
                    p.fill(ctx, *entityHandle._reg, entityHandle._entity, buf.data());

                    const uint32_t stride = sizeof(typename InstanceT::GPUData);
                    const uint32_t byteOffset = id * stride + p._offset;

                    auto span = _resourceManager.requestUploadOwned(
                        frameInstancePool._instancePoolViewHandle, p._size, 4, byteOffset);
                    std::memcpy(span.data(), buf.subspan(p._offset, p._size).data(), p._size);
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
    upload(_cameraInstancesPool);
    upload(pointLightInstancePool_);
}

void GPUInstanceManager::markDirty(const EntityHandle& handle, ComponentFlag componentFlag)
{
    auto* rID = handle._reg->try_get<RenderInstance_C>(handle._entity);
    if (!rID)
        return;

    switch (rID->_kind)
    {
        case InstanceKind::StaticMesh:
            _meshInstancesPool.dirtyComponents_[handle].setAll(componentFlag);
            break;
        case InstanceKind::Camera:
            _cameraInstancesPool.dirtyComponents_[handle].setAll(componentFlag);
            break;
        case InstanceKind::PointLight:
            pointLightInstancePool_.dirtyComponents_[handle].setAll(componentFlag);
    }
}
}  // namespace batap
