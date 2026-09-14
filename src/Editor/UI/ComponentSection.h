#pragma once

#include "UI/UITheme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>

namespace batap::ui
{
struct ComponentSection
{
    ComponentSection(const char* label, ComponentColor color, bool removable = false,
                     const char* summary = nullptr)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        bool* pOpen = ImGui::GetStateStorage()->GetBoolRef(ImGui::GetID(label), true);

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        // Content is inset by the window padding, rules are not.
        const float ruleEndX = ImGui::GetCurrentWindow()->InnerRect.Max.x;
        const float h = ImGui::GetFrameHeight();
        const float mid = pos.y + h * 0.5f;

        ImGui::InvisibleButton(label, {w, h});
        if (ImGui::IsItemClicked())
            *pOpen = !*pOpen;
        open_ = *pOpen;

        // Opened while the heading is still the last item: a caller cannot get
        // the ordering wrong.
        if (removable && ImGui::BeginPopupContextItem(label))
        {
            if (ImGui::MenuItem("Remove"))
                remove_ = true;
            ImGui::EndPopup();
        }

        const ImU32 hue = ImGui::GetColorU32(colorOf(color));
        const float r = h * 0.16f;
        const float cx = pos.x + h * 0.35f;
        if (open_)
            dl->AddTriangleFilled({cx - r, mid - r * 0.6f}, {cx + r, mid - r * 0.6f},
                                  {cx, mid + r * 0.8f}, hue);
        else
            dl->AddTriangleFilled({cx - r * 0.6f, mid - r}, {cx - r * 0.6f, mid + r},
                                  {cx + r * 0.8f, mid}, hue);

        const float textX = pos.x + h * 0.72f;
        ImFont* face = smallFont ? smallFont : ImGui::GetFont();
        const float faceSize = face->LegacySize;
        const ImVec2 labelSize = face->CalcTextSizeA(faceSize, FLT_MAX, 0.0f, label);
        // Drawn twice a half-pixel apart: the atlas holds one weight.
        const ImVec2 labelPos{textX, pos.y + (h - labelSize.y) * 0.5f};
        dl->AddText(face, faceSize, labelPos, hue, label);
        dl->AddText(face, faceSize, {labelPos.x + 0.5f, labelPos.y}, hue, label);
        float x = textX + labelSize.x + SectionRuleGap;

        if (!open_ && summary && summary[0] != '\0')
        {
            dl->AddText({x, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f},
                        ImGui::GetColorU32(textDim), summary);
            x += ImGui::CalcTextSize(summary).x + SectionRuleGap;
        }

        if (x < ruleEndX)
            dl->AddLine({x, mid}, {ruleEndX, mid}, ImGui::GetColorU32(border), 1.0f);

        if (open_)
            ImGui::Spacing();
    }

    bool removeClicked() const { return remove_; }

    explicit operator bool() const { return open_; }

    ComponentSection(const ComponentSection&)            = delete;
    ComponentSection& operator=(const ComponentSection&) = delete;

   private:
    bool open_ = false;
    bool remove_ = false;
};

}  // namespace batap::ui
