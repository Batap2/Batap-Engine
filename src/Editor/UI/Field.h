#pragma once

#include <imgui.h>
#include <imgui_internal.h>

// Usage:
//   if (auto _ = ui::BeginFields("id")) {
//       ui::Field("Position", [&]{ ... });
//   }

namespace batap::ui
{
struct BeginFields
{
    explicit BeginFields(const char* id)
    {
        active_ = ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit);
        if (active_)
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("widget", ImGuiTableColumnFlags_WidthStretch);
        }
    } 
    ~BeginFields() { if (active_) ImGui::EndTable(); }

    explicit operator bool() const { return active_; }

    BeginFields(const BeginFields&)            = delete;
    BeginFields& operator=(const BeginFields&) = delete;

private:
    bool active_;
};

template <typename Fn>
auto Field(const char* label, Fn&& drawWidget) -> decltype(drawWidget())
{
    IM_ASSERT_USER_ERROR(ImGui::GetCurrentTable() != nullptr, "ui::Field must be called inside ui::BeginFields");
    ImGui::PushID(label);
    struct Guard { ~Guard() { ImGui::PopID(); } } guard;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    return drawWidget();
}

inline bool FieldDragFloat(const char* label, float* v, float speed = 1.0f, float min = 0.0f,
                           float max = 0.0f)
{
    return Field(label, [=] {
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::DragFloat("##v", v, speed, min, max);
    });
}

inline bool FieldDragFloat3(const char* label, float* v, float speed = 1.0f)
{
    return Field(label, [=] {
        ImGui::SetNextItemWidth(-1.0f);
        return ImGui::DragFloat3("##v", v, speed);
    });
}

}  // namespace batap::ui
