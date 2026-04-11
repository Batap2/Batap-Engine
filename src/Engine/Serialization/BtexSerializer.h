#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Assets/Texture.h"

namespace batap
{

struct TextureDesc
{
    std::string       sourcePath;
    TextureColorSpace colorSpace = TextureColorSpace::SRGB;
    TextureFilter     filter     = TextureFilter::Linear;
    TextureWrap       wrapU      = TextureWrap::Repeat;
    TextureWrap       wrapV      = TextureWrap::Repeat;
    uint32_t          mipLevels  = 1;  // 0 = auto full mip chain
};

bool                       writeBtex(const TextureDesc& desc, const std::string& outPath);
bool                       writeBtex(const Texture& tex, const std::string& sourcePath, const std::string& outPath);
std::optional<TextureDesc> readBtex(const std::string& path);

}  // namespace batap
