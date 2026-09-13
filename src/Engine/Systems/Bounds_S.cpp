#include "Bounds_S.h"

#include "Assets/AssetManager.h"
#include "Assets/Mesh.h"
#include "Components/Mesh_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "Renderer/DebugDraw.h"
#include "World.h"

namespace batap
{

void Bounds_S::drawBounds(World& world, Engine& ctx)
{
    if (!showBounds_)
        return;

    DebugDraw& dbg = world.debugOverlay();
    auto& assetManager = *ctx.assetManager_;

    for (auto [e, meshC, tc] : world.registry_.view<Mesh_C, Transform_C>().each())
    {
        if (!meshC.mesh_)
            continue;

        const Mesh* mesh = assetManager.get(meshC.mesh_);
        if (!mesh || !mesh->localBounds_.valid())
            continue;

        const AABB bounds = mesh->localBounds_.transformed(tc.world());
        dbg.aabb(bounds.min_, bounds.max_, col3{0.2f, 0.9f, 0.4f});
    }
}

}  // namespace batap
