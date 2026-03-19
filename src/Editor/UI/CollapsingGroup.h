#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include "UITheme.h"

namespace batap::ui
{

inline constexpr float GroupPadding = 6.0f;

struct CollapsingGroup
{
    explicit CollapsingGroup(const char* label)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiStyle& style = ImGui::GetStyle();

        bool* pOpen = ImGui::GetStateStorage()->GetBoolRef(ImGui::GetID(label), true);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float h = ImGui::GetFrameHeight();

        ImGui::InvisibleButton(label, {w, h});
        if (ImGui::IsItemClicked())
            *pOpen = !*pOpen;
        open_ = *pOpen;

        ImU32 headerCol = ImGui::GetColorU32(ImGui::IsItemActive()    ? ImGuiCol_HeaderActive
                                             : ImGui::IsItemHovered() ? ImGuiCol_HeaderHovered
                                                                      : ImGuiCol_Header);
        dl->AddRectFilled(pos, {pos.x + w, pos.y + h}, headerCol, WindowRounding,
                          open_ ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);

        ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
        ImVec2 ac = {pos.x + h * 0.5f, pos.y + h * 0.5f};
        float r = h * 0.18f;
        if (open_)
            dl->AddTriangleFilled({ac.x - r, ac.y - r * 0.5f}, {ac.x + r, ac.y - r * 0.5f},
                                  {ac.x, ac.y + r}, textCol);
        else
            dl->AddTriangleFilled({ac.x - r * 0.5f, ac.y + r}, {ac.x - r * 0.5f, ac.y - r},
                                  {ac.x + r, ac.y}, textCol);

        dl->AddText({pos.x + h, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f}, textCol, label);

        x0_ = pos.x;
        x1_ = pos.x + w;

        if (open_)
        {
            startPos_ = ImGui::GetCursorScreenPos();
            startPos_.y -= style.ItemSpacing.y;

            splitter_.Split(dl, 2);
            splitter_.SetCurrentChannel(dl, 1);

            ImGuiWindow* win = ImGui::GetCurrentWindow();
            savedWorkRectMaxX_ = win->WorkRect.Max.x;
            savedContentRegionMaxX_ = win->ContentRegionRect.Max.x;
            win->WorkRect.Max.x -= GroupPadding;
            win->ContentRegionRect.Max.x -= GroupPadding;

            ImGui::Indent(GroupPadding);
            ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, startPos_.y + GroupPadding});
        }
    }

    ~CollapsingGroup()
    {
        if (!open_)
            return;

        ImGuiStyle& style = ImGui::GetStyle();
        ImVec2 cur = ImGui::GetCursorScreenPos();
        float endY = cur.y - style.ItemSpacing.y + GroupPadding;

        ImGui::SetCursorScreenPos({cur.x, endY});
        ImGui::Dummy({0, 0});
        ImGui::Unindent(GroupPadding);

        ImGuiWindow* win = ImGui::GetCurrentWindow();
        win->WorkRect.Max.x = savedWorkRectMaxX_;
        win->ContentRegionRect.Max.x = savedContentRegionMaxX_;

        auto* dl = ImGui::GetWindowDrawList();
        splitter_.SetCurrentChannel(dl, 0);
        dl->AddRectFilled({x0_, startPos_.y}, {x1_, endY}, IM_COL32(30, 30, 30, 120),
                          WindowRounding, ImDrawFlags_RoundCornersBottom);
        splitter_.Merge(dl);
    }

    explicit operator bool() const { return open_; }

    CollapsingGroup(const CollapsingGroup&) = delete;
    CollapsingGroup& operator=(const CollapsingGroup&) = delete;

   private:
    bool open_ = false;
    float x0_ = 0.0f;
    float x1_ = 0.0f;
    float savedWorkRectMaxX_ = 0.0f;
    float savedContentRegionMaxX_ = 0.0f;
    ImVec2 startPos_ = {};
    ImDrawListSplitter splitter_;
};

}  // namespace batap::ui
