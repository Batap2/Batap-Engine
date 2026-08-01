#include "InstanceManager.h"
#include <cstddef>
#include <cstdint>
#include "Components/ComponentFlag.h"
#include "Components/EntityHandle.h"
#include "Components/Kind_C.h"
#include "EntityKind.h"
#include "Renderer/Renderer.h"
#include "Renderer/ResourceManager.h"
#include "instanceDeclaration.h"

namespace batap
{
GPUInstanceManager::GPUInstanceManager(Engine& ctx)
    : _resourceManager(*ctx._renderer->_resourceManager) {};

void GPUInstanceManager::uploadRemainingFrameDirty(Engine& ctx)
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
                    const uint32_t stride = sizeof(typename InstanceT::GPUData);
                    const uint32_t byteOffset = id * stride + p._offset;

                    auto span = _resourceManager.requestUploadOwned(
                        frameInstancePool._instancePoolViewHandle, stride, 4, byteOffset, p._offset,
                        p._size);

                    p.fill(ctx, *entityHandle._reg, entityHandle._entity, span.data());
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

    pools_.forEach(upload);
}

void GPUInstanceManager::markDirty(const EntityHandle& handle, ComponentFlag componentFlag)
{
    // A CPU-only component has no GPU mirror to invalidate. Bailing out here
    // rather than at each call site is what lets generic code — the field
    // loops, Scene::write<T> — mark anything without knowing whether it is
    // mirrored, and avoids allocating a dirty entry for nothing.
    if (!any(componentFlag))
        return;

    auto* k = handle._reg->try_get<Kind_C>(handle._entity);
    if (!k)
        return;

    visitPool(k->value, [&](auto& pool) { pool.dirtyComponents_[handle].setAll(componentFlag); });
}
}  // namespace batap
