#include "AssetHolder.h"

#include <imgui.h>

#include <iostream>

namespace batap
{
bool AssetHolder(AssetHolderConfig config)
{
    bool clicked = false;
    if (ImGui::InvisibleButton("my_custom_widget", config.size_))
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
    return clicked;
}
}  // namespace batap
