#pragma once

#include <volk.h>

#include "Renderer/ResourceFormat.h"

#include <cassert>
#include <cstdint>

namespace batap
{
inline VkFormat toVkFormat(ResourceFormat format)
{
    switch (format)
    {
        case ResourceFormat::UNKNOWN:              return VK_FORMAT_UNDEFINED;
        case ResourceFormat::R32G32B32A32_FLOAT:   return VK_FORMAT_R32G32B32A32_SFLOAT;
        case ResourceFormat::R32G32B32_FLOAT:      return VK_FORMAT_R32G32B32_SFLOAT;
        case ResourceFormat::R32G32_FLOAT:         return VK_FORMAT_R32G32_SFLOAT;
        case ResourceFormat::R32_FLOAT:            return VK_FORMAT_R32_SFLOAT;
        case ResourceFormat::R32_UINT:             return VK_FORMAT_R32_UINT;
        case ResourceFormat::R16G16B16A16_FLOAT:   return VK_FORMAT_R16G16B16A16_SFLOAT;
        case ResourceFormat::R16_FLOAT:            return VK_FORMAT_R16_SFLOAT;
        case ResourceFormat::R16_UINT:             return VK_FORMAT_R16_UINT;
        case ResourceFormat::R8G8B8A8_UNORM:       return VK_FORMAT_R8G8B8A8_UNORM;
        case ResourceFormat::R8G8B8A8_UNORM_SRGB:  return VK_FORMAT_R8G8B8A8_SRGB;
        case ResourceFormat::B8G8R8A8_UNORM:       return VK_FORMAT_B8G8R8A8_UNORM;
        case ResourceFormat::R8_UNORM:             return VK_FORMAT_R8_UNORM;
        case ResourceFormat::D32_FLOAT:            return VK_FORMAT_D32_SFLOAT;
        case ResourceFormat::D16_UNORM:            return VK_FORMAT_D16_UNORM;
        case ResourceFormat::D24_UNORM_S8_UINT:    return VK_FORMAT_D32_SFLOAT_S8_UINT;
    }
    // No default case on purpose: adding a ResourceFormat must break the build
    // here rather than assert at runtime.
    assert(false && "toVkFormat: format outside the enum");
    return VK_FORMAT_UNDEFINED;
}

inline VkIndexType toVkIndexType(ResourceFormat format)
{
    switch (format)
    {
        case ResourceFormat::R16_UINT: return VK_INDEX_TYPE_UINT16;
        case ResourceFormat::R32_UINT: return VK_INDEX_TYPE_UINT32;
        default:
            assert(false && "toVkIndexType: not an index format");
            return VK_INDEX_TYPE_UINT32;
    }
}

inline uint32_t bytesPerPixel(ResourceFormat format)
{
    switch (format)
    {
        case ResourceFormat::R32G32B32A32_FLOAT:  return 16;
        case ResourceFormat::R16G16B16A16_FLOAT:  return 8;
        case ResourceFormat::R32_FLOAT:
        case ResourceFormat::R32_UINT:
        case ResourceFormat::R8G8B8A8_UNORM:
        case ResourceFormat::R8G8B8A8_UNORM_SRGB:
        case ResourceFormat::B8G8R8A8_UNORM:      return 4;
        case ResourceFormat::R16_FLOAT:
        case ResourceFormat::R16_UINT:            return 2;
        case ResourceFormat::R8_UNORM:            return 1;
        default:
            assert(false && "bytesPerPixel: unmapped format for upload");
            return 4;
    }
}
}  // namespace batap
