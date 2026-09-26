#pragma once

#include "EigenTypes.h"

#include <imgui.h>

// Two screen spaces, which only coincide on Windows:
// - pixels: the framebuffer. InputManager::mousePos(), Engine::getFrameSize(),
//   ScreenProjector, rayFromScreen, platformSetCursorPos all use them.
// - points: what ImGui draws in. On macOS Retina a point is 2 pixels.
// Screen math stays in pixels; convert here, at the ImGui boundary only.
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
