#include "EditorIcons.h"

#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Assets/Texture.h"
#include "Components/Camera_C.h"
#include "Components/PointLight_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "Paths.h"
#include "Renderer/Billboards.h"
#include "Shaders/ShaderInterop.h"
#include "World.h"

namespace batap
{
namespace
{

// Fraction of the viewport height, not world units.
constexpr float kIconSize = 0.045f;

uint32_t bindlessIndexOf(TextureHandle handle, Engine& ctx)
{
    if (!handle)
        return InvalidGPUIndex;
    const Texture* tex = ctx.assetManager_->get(handle);
    return tex ? tex->bindlessIndex_ : InvalidGPUIndex;
}

}  // namespace

void EditorIcons::draw(World& world, Engine& ctx)
{
    if (!show_)
        return;

    if (!loaded_)
    {
        loaded_ = true;
        // Engine-owned, not project-owned: an absolute path makes the asset
        // manager's project base dir drop out of the join.
        light_ = loadAsset<Texture>(resolveEngineFile("assets/icons/light.png",
                                                      "assets/icons/light.png"),
                                    ctx);
        camera_ = loadAsset<Texture>(resolveEngineFile("assets/icons/camera.png",
                                                       "assets/icons/camera.png"),
                                     ctx);
        if (auto unlit = ctx.assetManager_->getHandle<Material>(kUnlitMaterialPath))
            material_ = *unlit;
    }

    Billboards& out = world.billboards();
    const uint32_t lightTex = bindlessIndexOf(light_, ctx);
    const uint32_t cameraTex = bindlessIndexOf(camera_, ctx);
    const uint32_t materialIdx = material_ ? material_.index : InvalidGPUIndex;

    for (auto [e, light, tc] : world.registry_.view<PointLight_C, Transform_C>().each())
        out.add({.pos_ = tc.world().translation(),
                 .size_ = {kIconSize, kIconSize},
                 .tint_ = light.color_,
                 .textureIdx_ = lightTex,
                 .materialIdx_ = materialIdx,
                 .sizeMode_ = Billboards::SizeMode::Screen});

    for (auto [e, cam, tc] : world.registry_.view<Camera_C, Transform_C>().each())
    {
        // The camera being rendered from sits at the eye: its icon would fill
        // the screen or fall behind the near plane.
        if (cam.active_)
            continue;
        out.add({.pos_ = tc.world().translation(),
                 .size_ = {kIconSize, kIconSize},
                 .textureIdx_ = cameraTex,
                 .materialIdx_ = materialIdx,
                 .sizeMode_ = Billboards::SizeMode::Screen});
    }
}

}  // namespace batap
