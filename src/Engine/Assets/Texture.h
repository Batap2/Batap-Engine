#pragma once

#include <cstdint>
#include "Renderer/ResourceFormatWrapper.h"
#include "Renderer/ResourceManager.h"

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
    GPUViewHandle viewHandle_;
    uint32_t heapIdx_;
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
