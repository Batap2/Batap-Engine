#pragma once

#include <volk.h>

namespace batap
{
struct RenderTargets
{
    VkImage colorImage_ = VK_NULL_HANDLE;
    VkImageView color_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkImageView depth_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkClearColorValue clearColor_{};
};
}  // namespace batap
