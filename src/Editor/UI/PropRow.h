#pragma once

#include <imgui.h>

// Usage:
//   if (ui::PropRow("Position", [&]{ return ImGui::DragFloat3("##pos", data); })) { ... }
//   if (ui::PropFloat("Intensity", &val, 0.1f)) { ... }
//
// Affiche un label aligné à gauche sur une colonne fixe,
// puis le widget occupe le reste de la ligne.

namespace batap::ui
{
inline constexpr float kLabelWidth = 100.0f;

template <typename Fn>
auto PropRow(const char* label, Fn&& drawWidget) -> decltype(drawWidget())
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    return drawWidget();
}

inline bool PropFloat(const char* label, float* v,
                      float speed = 1.0f, float min = 0.0f, float max = 0.0f)
{
    return PropRow(label, [=]{ return ImGui::DragFloat("##v", v, speed, min, max); });
}

inline bool PropFloat3(const char* label, float* v, float speed = 1.0f)
{
    return PropRow(label, [=]{ return ImGui::DragFloat3("##v", v, speed); });
}

}  // namespace batap::ui
