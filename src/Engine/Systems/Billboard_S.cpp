#include "Systems/Billboard_S.h"

#include "Assets/AssetManager.h"
#include "Assets/Texture.h"
#include "Components/Billboard_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "Renderer/Billboards.h"
#include "Shaders/ShaderInterop.h"
#include "World.h"

namespace batap
{

void Billboard_S::submit(World& world, Engine& ctx)
{
    Billboards& out = world.billboards();

    for (auto [e, bb, tc] : world.registry_.view<Billboard_C, Transform_C>().each())
    {
        uint32_t textureIdx = InvalidGPUIndex;
        if (bb.texture_)
            if (const Texture* tex = ctx.assetManager_->get(bb.texture_))
                textureIdx = tex->bindlessIndex_;

        quatf rot = quatf::Identity();
        if (bb.orientation_ == Billboards::Orientation::None)
        {
            m3f basis = tc.world().linear();
            basis.colwise().normalize();
            rot = quatf(basis);
        }

        out.add({.pos_ = tc.world().translation(),
                 .rot_ = rot,
                 .size_ = {bb.width_, bb.height_},
                 .tint_ = bb.tint_,
                 .alpha_ = bb.alpha_,
                 .textureIdx_ = textureIdx,
                 .materialIdx_ = bb.material_ ? bb.material_.index : InvalidGPUIndex,
                 .sizeMode_ = bb.sizeMode_,
                 .orientation_ = bb.orientation_});
    }
}

}  // namespace batap
