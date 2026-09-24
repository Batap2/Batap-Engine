#include "EditorIcons.h"

#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Components/Camera_C.h"
#include "Components/EditorOnly_C.h"
#include "Components/PointLight_C.h"
#include "Components/SpotLight_C.h"
#include "Components/Transform_C.h"
#include "Engine.h"
#include "Paths.h"
#include "Renderer/Billboards.h"
#include "Renderer/ResourceManager.h"
#include "Shaders/ShaderInterop.h"
#include "Spatial/SpatialIndex.h"
#include "UI/IconsMaterialDesign.h"
#include "World.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb/stb_truetype.h>

namespace batap
{
namespace
{

// Fraction of the viewport height, not world units.
constexpr float kIconSize = 0.045f;

constexpr uint32_t kTexSize = 80;
constexpr uint32_t kMargin = 5;

// IconsMaterialDesign.h spells its icons as UTF-8 strings, for ImGui; stb wants
// a codepoint.
constexpr int glyph(std::string_view utf8)
{
    const auto b = [&](size_t i) { return static_cast<int>(static_cast<unsigned char>(utf8[i])); };
    if (b(0) < 0x80)
        return b(0);
    if (b(0) < 0xE0)
        return ((b(0) & 0x1F) << 6) | (b(1) & 0x3F);
    if (b(0) < 0xF0)
        return ((b(0) & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F);
    return ((b(0) & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);
}

// The glyph the scene tree already shows for that entity.
constexpr int kLightGlyph = glyph(ICON_MD_LIGHTBULB);
constexpr int kCameraGlyph = glyph(ICON_MD_VIDEOCAM);

std::vector<unsigned char> readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return {};
    const std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

std::vector<uint8_t> rasterise(const stbtt_fontinfo& font, int codepoint)
{
    const float fit = static_cast<float>(kTexSize - 2 * kMargin);

    float scale = stbtt_ScaleForPixelHeight(&font, fit);
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* raw = stbtt_GetCodepointBitmap(&font, 0, scale, codepoint, &w, &h, &xoff, &yoff);

    // ScaleForPixelHeight works on the font's vertical metrics, not on this
    // glyph's ink, so a wide or tall glyph can still overflow the square.
    if (raw && (w > static_cast<int>(fit) || h > static_cast<int>(fit)))
    {
        scale *= fit / static_cast<float>(std::max(w, h));
        stbtt_FreeBitmap(raw, nullptr);
        raw = stbtt_GetCodepointBitmap(&font, 0, scale, codepoint, &w, &h, &xoff, &yoff);
    }
    if (!raw || w <= 0 || h <= 0)
    {
        stbtt_FreeBitmap(raw, nullptr);
        return {};
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
    const std::span<const unsigned char> coverage{raw,
                                                  static_cast<size_t>(w) * static_cast<size_t>(h)};
#pragma clang diagnostic pop
    std::vector<uint8_t> rgba(static_cast<size_t>(kTexSize) * kTexSize * 4, 0);
    const std::span<uint8_t> pixels{rgba};

    const int originX = (static_cast<int>(kTexSize) - w) / 2;
    const int originY = (static_cast<int>(kTexSize) - h) / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const size_t dst =
                (static_cast<size_t>(originY + y) * kTexSize + static_cast<size_t>(originX + x)) *
                4;
            pixels[dst + 0] = 255;
            pixels[dst + 1] = 255;
            pixels[dst + 2] = 255;
            pixels[dst + 3] =
                coverage[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
        }

    stbtt_FreeBitmap(raw, nullptr);
    return rgba;
}

}  // namespace

EditorIcons::~EditorIcons()
{
    if (!resources_)
        return;
    resources_->requestDestroy(light_.texture_);
    resources_->requestDestroy(camera_.texture_);
}

void EditorIcons::load(Engine& ctx)
{
    loaded_ = true;
    resources_ = ctx.assetManager_->resourceManager_;

    const std::vector<unsigned char> ttf = readFile(
        resolveEngineFile("assets/MaterialIcons-Regular.ttf", "assets/MaterialIcons-Regular.ttf"));
    if (ttf.empty())
    {
        std::cerr << "[EditorIcons] MaterialIcons-Regular.ttf not found\n";
        return;
    }

    stbtt_fontinfo font{};
    if (!stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
    {
        std::cerr << "[EditorIcons] stbtt_InitFont failed\n";
        return;
    }

    auto makeIcon = [&](int codepoint, const char* name) -> Icon
    {
        const std::vector<uint8_t> rgba = rasterise(font, codepoint);
        if (rgba.empty())
            return {};

        Icon icon;
        icon.texture_ =
            resources_->createImage2D(kTexSize, kTexSize, ResourceFormat::R8G8B8A8_UNORM, name);
        const std::span<std::byte> staging = resources_->requestTextureUpload(
            icon.texture_, kTexSize, kTexSize, ResourceFormat::R8G8B8A8_UNORM);
        std::memcpy(staging.data(), rgba.data(), std::min(staging.size(), rgba.size()));
        icon.bindlessIndex_ = resources_->textureIndex(icon.texture_);
        return icon;
    };

    light_ = makeIcon(kLightGlyph, "editorIcon_light");
    camera_ = makeIcon(kCameraGlyph, "editorIcon_camera");

    if (auto unlit = ctx.assetManager_->getHandle<Material>(kUnlitMaterialPath))
        material_ = *unlit;
}

void EditorIcons::draw(World& world, Engine& ctx)
{
    if (!show_)
        return;
    if (!loaded_)
        load(ctx);

    Billboards& out = world.billboards();
    const uint32_t materialIdx = material_ ? material_.index : InvalidGPUIndex;

    const auto lightIcon = [&](const Transform_C& tc, const col3& tint)
    {
        out.add({.pos_ = tc.world().translation(),
                 .size_ = {kIconSize, kIconSize},
                 .tint_ = tint,
                 .textureIdx_ = light_.bindlessIndex_,
                 .materialIdx_ = materialIdx,
                 .sizeMode_ = Billboards::SizeMode::Screen});
    };
    for (auto [e, light, tc] : world.registry_.view<PointLight_C, Transform_C>().each())
        lightIcon(tc, light.color_);
    for (auto [e, spot, tc] : world.registry_.view<SpotLight_C, Transform_C>().each())
        lightIcon(tc, spot.color_);

    const entt::entity eye = world.renderCamera();
    for (entt::entity e : world.registry_.view<Camera_C, Transform_C>())
    {
        // The camera being rendered from sits at the eye: its icon would fill
        // the screen or fall behind the near plane.
        if (e == eye || world.registry_.all_of<EditorOnly_C>(e))
            continue;
        out.add({.pos_ = world.registry_.get<Transform_C>(e).world().translation(),
                 .size_ = {kIconSize, kIconSize},
                 .textureIdx_ = camera_.bindlessIndex_,
                 .materialIdx_ = materialIdx,
                 .sizeMode_ = Billboards::SizeMode::Screen});
    }
}

namespace
{
bool hasIcon(World& world, entt::entity e)
{
    auto& reg = world.registry_;
    if (reg.all_of<PointLight_C, Transform_C>(e) || reg.all_of<SpotLight_C, Transform_C>(e))
        return true;
    return reg.all_of<Camera_C, Transform_C>(e) && !reg.all_of<EditorOnly_C>(e) &&
           e != world.renderCamera();
}

BillboardQuad iconQuad(const v3f& pos, const CameraBasis& cam)
{
    return billboardQuad(pos, quatf::Identity(), v2f(kIconSize, kIconSize),
                         Billboards::SizeMode::Screen, Billboards::Orientation::Spherical, cam);
}
}  // namespace

std::optional<AABB> EditorIcons::boundsOf(World& world, entt::entity e) const
{
    auto& reg = world.registry_;
    if (!show_ || !reg.valid(e) || !hasIcon(world, e))
        return std::nullopt;

    const auto cam = renderCameraBasis(reg);
    if (!cam)
        return std::nullopt;

    const BillboardQuad quad = iconQuad(reg.get<Transform_C>(e).world().translation(), *cam);
    AABB out;
    for (const float u : {-1.f, 1.f})
        for (const float v : {-1.f, 1.f})
            out.extend(quad.corner(u, v));
    return out;
}

RayHit EditorIcons::raycast(World& world, const Ray& ray, float maxT) const
{
    RayHit best;
    if (!show_)
        return best;

    const auto cam = renderCameraBasis(world.registry_);
    if (!cam)
        return best;

    const auto test = [&](entt::entity e, const v3f& pos)
    {
        const QuadHit hit = rayQuad(ray, iconQuad(pos, *cam), maxT);
        if (!hit.hit_)
            return;

        maxT = hit.t_;
        best.entity_ = e;
        best.t_ = hit.t_;
        best.point_ = hit.point_;
        best.normal_ = hit.normal_;
    };

    for (auto [e, light, tc] : world.registry_.view<PointLight_C, Transform_C>().each())
        test(e, tc.world().translation());
    for (auto [e, spot, tc] : world.registry_.view<SpotLight_C, Transform_C>().each())
        test(e, tc.world().translation());

    for (entt::entity e : world.registry_.view<Camera_C, Transform_C>())
        if (hasIcon(world, e))
            test(e, world.registry_.get<Transform_C>(e).world().translation());

    return best;
}

}  // namespace batap
