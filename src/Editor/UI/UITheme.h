#pragma once

#include "Reflection/ComponentRegistry.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <span>

namespace batap::ui
{

// -------------------------------------------------------------------------
// Theme constants — use these instead of hardcoding values elsewhere
// -------------------------------------------------------------------------
inline constexpr float  WindowRounding    = 6.0f;
inline constexpr float  ChildRounding     = 4.0f;
inline constexpr float  FrameRounding     = 2.0f;
inline constexpr float  PopupRounding     = 4.0f;
inline constexpr float  ScrollbarRounding = 4.0f;
inline constexpr float  GrabRounding      = 4.0f;
inline constexpr float  TabRounding       = 4.0f;

inline constexpr ImVec2 FramePadding      = {8.0f,  3.0f};
inline constexpr ImVec2 ItemSpacing       = {8.0f,  6.0f};
inline constexpr ImVec2 ItemInnerSpacing  = {6.0f,  4.0f};
inline constexpr ImVec2 WindowPadding     = {10.0f, 10.0f};
inline constexpr float  IndentSpacing     = 16.0f;
inline constexpr float  ScrollbarSize     = 10.0f;
inline constexpr float  GrabMinSize       = 8.0f;

inline constexpr float  WindowBorderSize  = 0.0f;
inline constexpr float  FrameBorderSize   = 0.0f;
inline constexpr float  PopupBorderSize   = 1.0f;

inline constexpr float  LabelColumnWidth  = 62.0f;
inline constexpr float  AxisBarWidth      = 3.0f;
inline constexpr float  SectionRuleGap    = 8.0f;

// A line box reserves a descender a label like "Cube.001" never uses, so
// centring the box leaves the ink high.
inline float TextOpticalOffsetY()
{
    const ImFontBaked* baked = ImGui::GetFontBaked();
    return baked ? -baked->Descent * 0.5f : 0.0f;
}

inline constexpr ImVec4 accent       = {0.15f, 0.55f, 0.82f, 1.00f};
inline constexpr ImVec4 accentHover  = {0.28f, 0.65f, 0.92f, 1.00f};
inline constexpr ImVec4 accentActive = {0.08f, 0.43f, 0.68f, 1.00f};

inline constexpr ImVec4 bg0   = {0.00f, 0.17f, 0.21f, 1.00f};  // base03 — main window
inline constexpr ImVec4 bg1   = {0.03f, 0.21f, 0.26f, 1.00f};  // base02 — child / popup
inline constexpr ImVec4 bg2   = {0.08f, 0.26f, 0.31f, 1.00f};  // frame (input, combo...)
inline constexpr ImVec4 bg3   = {0.14f, 0.31f, 0.36f, 1.00f};  // frame hovered
inline constexpr ImVec4 bg4   = {0.20f, 0.37f, 0.42f, 1.00f};  // frame active

inline constexpr ImVec4 hdr   = {0.05f, 0.23f, 0.28f, 1.00f};
inline constexpr ImVec4 hdrHv = {0.09f, 0.27f, 0.32f, 1.00f};
inline constexpr ImVec4 hdrAc = {0.14f, 0.32f, 0.37f, 1.00f};

inline constexpr ImVec4 border = {0.09f, 0.29f, 0.34f, 1.00f};

inline constexpr ImVec4 text      = {0.58f, 0.63f, 0.63f, 1.00f};  // base1
inline constexpr ImVec4 textDim   = {0.35f, 0.43f, 0.46f, 1.00f};  // base01
inline constexpr ImVec4 textBright = {0.93f, 0.91f, 0.84f, 1.00f}; // base2

inline constexpr ImVec4 yellow  = {0.71f, 0.54f, 0.00f, 1.00f};
inline constexpr ImVec4 orange  = {0.80f, 0.29f, 0.09f, 1.00f};
inline constexpr ImVec4 red     = {0.86f, 0.20f, 0.18f, 1.00f};
inline constexpr ImVec4 magenta = {0.83f, 0.21f, 0.51f, 1.00f};
inline constexpr ImVec4 violet  = {0.42f, 0.44f, 0.77f, 1.00f};
inline constexpr ImVec4 blue    = {0.15f, 0.55f, 0.82f, 1.00f};
inline constexpr ImVec4 cyan    = {0.16f, 0.63f, 0.60f, 1.00f};
inline constexpr ImVec4 green   = {0.52f, 0.60f, 0.00f, 1.00f};

inline ImFont* smallFont = nullptr;
inline ImFont* monoFont = nullptr;

struct PushSmallFont
{
    PushSmallFont() : pushed_(smallFont != nullptr)
    {
        if (pushed_)
            ImGui::PushFont(smallFont, smallFont->LegacySize);
    }
    ~PushSmallFont() { if (pushed_) ImGui::PopFont(); }

    PushSmallFont(const PushSmallFont&)            = delete;
    PushSmallFont& operator=(const PushSmallFont&) = delete;

   private:
    bool pushed_;
};

inline ImVec4 colorOf(ComponentColor c)
{
    switch (c)
    {
        case ComponentColor::Yellow:  return yellow;
        case ComponentColor::Orange:  return orange;
        case ComponentColor::Red:     return red;
        case ComponentColor::Magenta: return magenta;
        case ComponentColor::Violet:  return violet;
        case ComponentColor::Blue:    return blue;
        case ComponentColor::Cyan:    return cyan;
        case ComponentColor::Green:   return green;
        case ComponentColor::Neutral: break;
    }
    return text;
}

inline ImVec4 axisColor(int axis)
{
    switch (axis)
    {
        case 0:  return red;
        case 1:  return green;
        default: return blue;
    }
}

inline void ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();

    // -------------------------------------------------------------------------
    // Shape
    // -------------------------------------------------------------------------
    s.WindowRounding        = WindowRounding;
    s.ChildRounding         = ChildRounding;
    s.FrameRounding         = FrameRounding;
    s.PopupRounding         = PopupRounding;
    s.ScrollbarRounding     = ScrollbarRounding;
    s.GrabRounding          = GrabRounding;
    s.TabRounding           = TabRounding;

    s.FramePadding          = FramePadding;
    s.ItemSpacing           = ItemSpacing;
    s.ItemInnerSpacing      = ItemInnerSpacing;
    s.WindowPadding         = WindowPadding;
    s.IndentSpacing         = IndentSpacing;
    s.ScrollbarSize         = ScrollbarSize;
    s.GrabMinSize           = GrabMinSize;

    s.WindowBorderSize      = WindowBorderSize;
    s.FrameBorderSize       = FrameBorderSize;
    s.PopupBorderSize       = PopupBorderSize;

    // Apply colors
    std::span<ImVec4> c{s.Colors, ImGuiCol_COUNT};

    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = textDim;

    c[ImGuiCol_WindowBg]              = bg0;
    c[ImGuiCol_ChildBg]               = bg1;
    c[ImGuiCol_PopupBg]               = bg1;

    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = {0, 0, 0, 0};

    c[ImGuiCol_FrameBg]               = bg2;
    c[ImGuiCol_FrameBgHovered]        = bg3;
    c[ImGuiCol_FrameBgActive]         = bg4;

    c[ImGuiCol_TitleBg]               = bg0;
    c[ImGuiCol_TitleBgActive]         = bg1;
    c[ImGuiCol_TitleBgCollapsed]      = bg0;

    c[ImGuiCol_MenuBarBg]             = bg1;
    c[ImGuiCol_ScrollbarBg]           = bg0;
    c[ImGuiCol_ScrollbarGrab]         = bg3;
    c[ImGuiCol_ScrollbarGrabHovered]  = bg4;
    c[ImGuiCol_ScrollbarGrabActive]   = accent;

    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accentActive;

    c[ImGuiCol_Button]                = bg3;
    c[ImGuiCol_ButtonHovered]         = accent;
    c[ImGuiCol_ButtonActive]          = accentActive;

    c[ImGuiCol_Header]                = hdr;
    c[ImGuiCol_HeaderHovered]         = hdrHv;
    c[ImGuiCol_HeaderActive]          = hdrAc;

    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = accentHover;
    c[ImGuiCol_SeparatorActive]       = accent;

    c[ImGuiCol_ResizeGrip]            = {0, 0, 0, 0};
    c[ImGuiCol_ResizeGripHovered]     = accentHover;
    c[ImGuiCol_ResizeGripActive]      = accent;

    c[ImGuiCol_Tab]                   = bg1;
    c[ImGuiCol_TabHovered]            = hdrHv;
    c[ImGuiCol_TabSelected]           = bg2;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmed]             = bg0;
    c[ImGuiCol_TabDimmedSelected]     = bg1;

    c[ImGuiCol_DragDropTarget]        = accentHover;

    c[ImGuiCol_NavCursor]             = accent;
    c[ImGuiCol_NavWindowingHighlight] = accentHover;
    c[ImGuiCol_NavWindowingDimBg]     = {0.08f, 0.08f, 0.08f, 0.70f};
    c[ImGuiCol_ModalWindowDimBg]      = {0.08f, 0.08f, 0.08f, 0.70f};
}

}  // namespace batap::ui
