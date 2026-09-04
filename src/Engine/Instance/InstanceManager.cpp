#include "InstanceManager.h"
#include <cstdint>
#include <cstring>
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
    : resourceManager_(*ctx.renderer_->resourceManager_) {};

void GPUInstanceManager::uploadRemainingFrameDirty(Engine& ctx)
{
    auto frameIndex = ctx.getFrameindex();
    auto upload = [&](auto& frameInstancePool)
    {
        using PoolT = std::remove_reference_t<decltype(frameInstancePool)>;
        using InstanceT = typename PoolT::InstanceType;
        using GPUData = typename InstanceT::GPUData;

        auto& map = frameInstancePool.dirtyInstances_;
        for (auto it = map.begin(); it != map.end();)
        {
            const EntityHandle& entityHandle = it->first;
            FrameDirtyFlag& frameDirtyFlag = it->second;
            if (!frameDirtyFlag.dirty(frameIndex))
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

            // Built on the stack so staging is only ever written linearly.
            GPUData data{};
            InstanceFill<InstanceT>::fill(ctx, *entityHandle.reg_, entityHandle.entity_, data);

            auto span = resourceManager_.requestUpload(frameInstancePool.instancePoolHandle_,
                                                       sizeof(GPUData), id * sizeof(GPUData));
            std::memcpy(span.data(), &data, sizeof(GPUData));

            frameDirtyFlag.clear(frameIndex);

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
    // Bailing out here rather than at each call site lets generic code —
    // the field loops, Scene::write<T> — mark anything unconditionally.
    if (!any(componentFlag))
        return;

    auto* k = handle.reg_->try_get<Kind_C>(handle.entity_);
    if (!k)
        return;

    visitPool(k->value,
              [&](auto& pool)
              {
                  if (!any(componentFlag & pool.instanceUsedComponentFlag_))
                      return;

                  pool.dirtyInstances_[handle].setAll();
              });
}
}  // namespace batap
