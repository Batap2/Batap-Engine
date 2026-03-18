#pragma once

#include <imgui.h>
#include <span>

namespace batap::ui
{

inline void ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();

    // -------------------------------------------------------------------------
    // Shape
    // -------------------------------------------------------------------------
    s.WindowRounding        = 6.0f;
    s.ChildRounding         = 4.0f;
    s.FrameRounding         = 4.0f;
    s.PopupRounding         = 4.0f;
    s.ScrollbarRounding     = 4.0f;
    s.GrabRounding          = 4.0f;
    s.TabRounding           = 4.0f;

    s.FramePadding          = ImVec2(8.0f, 4.0f);
    s.ItemSpacing           = ImVec2(8.0f, 6.0f);
    s.ItemInnerSpacing      = ImVec2(6.0f, 4.0f);
    s.WindowPadding         = ImVec2(10.0f, 10.0f);
    s.IndentSpacing         = 16.0f;
    s.ScrollbarSize         = 10.0f;
    s.GrabMinSize           = 8.0f;

    s.WindowBorderSize      = 0.0f;
    s.FrameBorderSize       = 0.0f;
    s.PopupBorderSize       = 1.0f;

    // -------------------------------------------------------------------------
    // Palette  —  Solarized Dark
    //   base03 #002b36  base02 #073642  base01 #586e75  base0 #839496
    //   blue   #268bd2  cyan   #2aa198  yellow #b58900  orange #cb4b16
    // -------------------------------------------------------------------------
    // Accent — blue (#268bd2)
    constexpr ImVec4 accent       = {0.15f, 0.55f, 0.82f, 1.00f};
    constexpr ImVec4 accentHover  = {0.28f, 0.65f, 0.92f, 1.00f};
    constexpr ImVec4 accentActive = {0.08f, 0.43f, 0.68f, 1.00f};

    // Backgrounds
    constexpr ImVec4 bg0   = {0.00f, 0.17f, 0.21f, 1.00f};  // base03 — main window
    constexpr ImVec4 bg1   = {0.03f, 0.21f, 0.26f, 1.00f};  // base02 — child / popup
    constexpr ImVec4 bg2   = {0.08f, 0.26f, 0.31f, 1.00f};  // frame (input, combo...)
    constexpr ImVec4 bg3   = {0.14f, 0.31f, 0.36f, 1.00f};  // frame hovered
    constexpr ImVec4 bg4   = {0.20f, 0.37f, 0.42f, 1.00f};  // frame active

    // Headers (CollapsingHeader, TreeNode...)
    constexpr ImVec4 hdr   = {0.05f, 0.23f, 0.28f, 1.00f};
    constexpr ImVec4 hdrHv = {0.09f, 0.27f, 0.32f, 1.00f};
    constexpr ImVec4 hdrAc = {0.14f, 0.32f, 0.37f, 1.00f};

    // Border / separator
    constexpr ImVec4 border = {0.09f, 0.29f, 0.34f, 1.00f};

    // Text — base1 (#93a1a1) / base01 (#586e75)
    constexpr ImVec4 text    = {0.58f, 0.63f, 0.63f, 1.00f};
    constexpr ImVec4 textDim = {0.35f, 0.43f, 0.46f, 1.00f};

    
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
