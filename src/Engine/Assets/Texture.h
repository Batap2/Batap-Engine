#pragma once

#include <cstdint>
#include "Renderer/ResourceFormat.h"
#include "Renderer/ResourceManager.h"
#include "Renderer/SkyIrradiance.h"

namespace batap
{
enum class TextureColorSpace : uint8_t
{
    SRGB,    // albedo, emissive
    Linear,  // normal, roughness, metallic, masks
};

enum class TextureFilter : uint8_t
{
    Linear,
    Nearest,
    Anisotropic,
};

enum class TextureWrap : uint8_t
{
    Repeat,
    Clamp,
    Mirror,
};

struct Texture
{
    uint32_t bindlessIndex_;
    SH9 irradianceSH_;  // SH L2 irradiance, calculé au chargement pour les textures HDR
    ResourceFormat format_;
    uint32_t sizeX_;
    uint32_t sizeY_;
    uint32_t mipLevels_ = 1;
    TextureColorSpace colorSpace_ = TextureColorSpace::SRGB;
    TextureFilter filter_ = TextureFilter::Linear;
    TextureWrap wrapU_ = TextureWrap::Repeat;
    TextureWrap wrapV_ = TextureWrap::Repeat;
};
}  // namespace batap
