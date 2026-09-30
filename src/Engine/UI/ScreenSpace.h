#pragma once

#include "EigenTypes.h"

#include <imgui.h>

// Screen math is in framebuffer pixels; ImGui draws in points, 2 pixels on
// Retina. Convert only at the ImGui boundary.
namespace batap
{

inline float pixelsPerPoint()
{
    return ImGui::GetIO().DisplayFramebufferScale.x;
}

inline v2f toPoints(const v2f& pixels)
{
    return pixels / pixelsPerPoint();
}

inline v2f toPixels(const v2f& points)
{
    return points * pixelsPerPoint();
}

}  // namespace batap
