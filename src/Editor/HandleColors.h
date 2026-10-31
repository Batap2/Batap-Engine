#pragma once

#include <imgui.h>

#include <array>

namespace batap
{
inline constexpr std::array<ImVec4, 3> kAxisColors{ImVec4{0.91f, 0.29f, 0.33f, 1.f},
                                                   ImVec4{0.55f, 0.79f, 0.24f, 1.f},
                                                   ImVec4{0.25f, 0.57f, 0.94f, 1.f}};
inline constexpr ImVec4 kLitColor{1.f, 0.88f, 0.47f, 1.f};

inline ImVec4 mixColor(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t};
}

inline ImU32 fade(const ImVec4& base, float alphaScale)
{
    ImVec4 c = base;
    c.w *= alphaScale;
    return ImGui::GetColorU32(c);
}
}  // namespace batap
