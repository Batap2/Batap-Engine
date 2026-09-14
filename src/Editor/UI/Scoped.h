#pragma once

#include <imgui.h>

#include <initializer_list>
#include <utility>

namespace batap::ui
{
struct ScopedID
{
    explicit ScopedID(const char* id) { ImGui::PushID(id); }
    explicit ScopedID(int id) { ImGui::PushID(id); }
    ~ScopedID() { ImGui::PopID(); }

    ScopedID(const ScopedID&)            = delete;
    ScopedID& operator=(const ScopedID&) = delete;
};

struct ScopedColor
{
    ScopedColor(std::initializer_list<std::pair<ImGuiCol, ImVec4>> colors)
        : count_(static_cast<int>(colors.size()))
    {
        for (const auto& [idx, col] : colors)
            ImGui::PushStyleColor(idx, col);
    }
    ~ScopedColor() { ImGui::PopStyleColor(count_); }

    ScopedColor(const ScopedColor&)            = delete;
    ScopedColor& operator=(const ScopedColor&) = delete;

   private:
    int count_;
};

struct ScopedFont
{
    explicit ScopedFont(ImFont* font) : pushed_(font != nullptr)
    {
        if (pushed_)
            ImGui::PushFont(font, font->LegacySize);
    }
    ~ScopedFont()
    {
        if (pushed_)
            ImGui::PopFont();
    }

    ScopedFont(const ScopedFont&)            = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;

   private:
    bool pushed_;
};

}  // namespace batap::ui
