#pragma once

#include <volk.h>

namespace batap
{
struct RenderTargets
{
    VkImageView color_ = VK_NULL_HANDLE;
    VkImageView depth_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkClearColorValue clearColor_{};
};
}  // namespace batap
