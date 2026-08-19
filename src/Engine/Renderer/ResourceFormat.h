#pragma once

#include <cstdint>

namespace batap
{
// Backend-neutral format list.
enum class ResourceFormat : uint32_t
{
    UNKNOWN = 0,

    R32G32B32A32_FLOAT,
    R32G32B32_FLOAT,
    R32G32_FLOAT,
    R32_FLOAT,
    R32_UINT,

    R16G16B16A16_FLOAT,
    R16_FLOAT,
    R16_UINT,

    R8G8B8A8_UNORM,
    R8G8B8A8_UNORM_SRGB,
    B8G8R8A8_UNORM,
    R8_UNORM,

    D32_FLOAT,
    D24_UNORM_S8_UINT,
    D16_UNORM,
};
}  // namespace batap
