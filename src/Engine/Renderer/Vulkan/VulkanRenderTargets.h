#pragma once

#include <volk.h>

#include "Renderer/Vulkan/RenderGraph.h"

namespace batap
{
struct RenderTargets
{
    GraphImage color_;
    GraphImage depth_;
    VkExtent2D extent_{};
    VkClearColorValue clearColor_{};
};
}  // namespace batap
