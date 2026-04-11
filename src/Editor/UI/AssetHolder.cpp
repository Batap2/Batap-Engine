#include "AssetHolder.h"

#include <imgui.h>

#include <iostream>

namespace batap
{
bool AssetHolder(AssetHolderConfig config)
{
    // Each widget gets a unique ID from its screen position so multiple AssetHolders
    // in the same frame don't share the same ImGui ID.
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::PushID(static_cast<int>(cursor.x) * 10000 + static_cast<int>(cursor.y));
    bool clicked = false;
    if (ImGui::InvisibleButton("##asset", config.size_))
    {
        clicked = true;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetItemRectMin();
    ImVec2 p_max = ImGui::GetItemRectMax();

    ImColor col = ImColor(50,50,50,50);
    if(config._thumbnail){
        col = ImColor(200, 200, 200, 50);
    }

    draw_list->AddRectFilled(p, p_max, col, 4);

    if (!config.label_.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", config.label_.c_str());

    if (!config.label_.empty())
    {
        const char* text     = config.label_.c_str();
        ImVec2      textSize = ImGui::CalcTextSize(text);
        ImVec2      textPos  = {
            p.x + ((p_max.x - p.x) - textSize.x) * 0.5f,
            p_max.y - textSize.y - 2.0f
        };
        draw_list->PushClipRect(p, p_max, true);
        draw_list->AddText(textPos, IM_COL32(220, 220, 220, 230), text);
        draw_list->PopClipRect();
    }

    ImGui::PopID();
    return clicked;
}
}  // namespace batap
