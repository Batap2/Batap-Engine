#pragma once

#include <volk.h>

namespace batap
{
// The images a pass renders into, handed over by whoever owns them. It exists
// so a pass never has to know about the Renderer: one scope per pass means the
// targets are the only thing they need to agree on.
struct RenderTargets
{
    VkImageView color_ = VK_NULL_HANDLE;
    VkImageView depth_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkClearColorValue clearColor_{};
};
}  // namespace batap
