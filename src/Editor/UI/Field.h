#pragma once

#include "UI/UITheme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>
#include <cstddef>
#include <span>
#include <string_view>

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
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ItemSpacing.x, 4.0f});
        active_ = ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit);
        if (active_)
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, LabelColumnWidth);
            ImGui::TableSetupColumn("widget", ImGuiTableColumnFlags_WidthStretch);
        }
    }
    ~BeginFields()
    {
        if (active_)
            ImGui::EndTable();
        ImGui::PopStyleVar();
    }

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
    const float avail = ImGui::GetContentRegionAvail().x;
    const float w = ImGui::CalcTextSize(label).x;
    if (w <= avail)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
        ImGui::TextUnformatted(label);
    }
    else
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), p,
                                  {p.x + avail, p.y + ImGui::GetTextLineHeight()}, p.x + avail,
                                  label, nullptr, nullptr);
        ImGui::Dummy({avail, ImGui::GetTextLineHeight()});
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", label);
    }
    ImGui::TableSetColumnIndex(1);
    return drawWidget();
}

inline void WrapDragMouse()
{
    if (!ImGui::IsItemActive() || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pos = ImGui::GetIO().MousePos;
    const float minX = vp->Pos.x, maxX = vp->Pos.x + vp->Size.x - 1;
    const float minY = vp->Pos.y, maxY = vp->Pos.y + vp->Size.y - 1;

    ImVec2 wrapped = pos;
    bool wrap = false;
    if (pos.x <= minX)      { wrapped.x = maxX - 1; wrap = true; }
    else if (pos.x >= maxX) { wrapped.x = minX + 1; wrap = true; }
    if (pos.y <= minY)      { wrapped.y = maxY - 1; wrap = true; }
    else if (pos.y >= maxY) { wrapped.y = minY + 1; wrap = true; }

    if (wrap)
        ImGui::TeleportMousePos(wrapped);
}

// %g would print "2.264e-06" for a near-zero and overflow the field.
inline void formatTrimmed(std::span<char> buf, float v)
{
    if (v == 0.0f)  // also catches -0
    {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    ImFormatString(buf.data(), buf.size(), "%.3f", static_cast<double>(v));

    std::string_view sv{buf.data()};
    if (sv.find('.') == std::string_view::npos)
        return;
    size_t end = sv.find_last_not_of('0');
    if (sv[end] == '.')
        --end;
    buf[end + 1] = '\0';

    if (std::string_view{buf.data()} == "-0")
    {
        buf[0] = '0';
        buf[1] = '\0';
    }
}

// No style aligns a drag's value, so the widget prints nothing and the text is
// redrawn here. The format comes back while ImGui owns the text, being typed.
inline bool DragValue(const char* id, float* v, float width, float speed, float min, float max,
                      const char* fmt = nullptr)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    const bool editing = ImGui::TempInputIsActive(ImGui::GetID(id));

    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::DragFloat(id, v, speed, min, max, editing ? "%.3f" : "",
                                          ImGuiSliderFlags_NoRoundToFormat);
    WrapDragMouse();

    if (!editing)
    {
        char buf[32];
        if (fmt)
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
            ImFormatString(buf, sizeof(buf), fmt, static_cast<double>(*v));
#pragma clang diagnostic pop
        }
        else
        {
            formatTrimmed(buf, *v);
        }

        ImFont* face = monoFont ? monoFont : ImGui::GetFont();
        const float faceSize = face->LegacySize;
        const ImVec2 sz = face->CalcTextSizeA(faceSize, FLT_MAX, 0.0f, buf);
        const float pad = ImGui::GetStyle().FramePadding.x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = ImGui::GetItemRectSize().x;
        dl->PushClipRect({p.x + AxisBarWidth, p.y}, {p.x + w - pad * 0.5f, p.y + h}, true);
        dl->AddText(face, faceSize, {p.x + w - pad - sz.x, p.y + (h - sz.y) * 0.5f},
                    ImGui::GetColorU32(ImGuiCol_Text), buf);
        dl->PopClipRect();
    }
    return changed;
}

inline bool DragFloatAxis(const char* id, float* v, int axis, float width, float speed,
                          float min = 0.0f, float max = 0.0f)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    const bool changed = DragValue(id, v, width, speed, min, max);

    ImGui::GetWindowDrawList()->AddRectFilled({p.x, p.y}, {p.x + AxisBarWidth, p.y + h},
                                              ImGui::GetColorU32(axisColor(axis)), FrameRounding,
                                              ImDrawFlags_RoundCornersLeft);
    return changed;
}

inline bool DragFloatN(std::span<float> v, float speed = 0.01f, float min = 0.0f,
                       float max = 0.0f)
{
    const auto count = static_cast<float>(v.size());
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float w = (ImGui::GetContentRegionAvail().x - spacing * (count - 1.0f)) / count;

    bool changed = false;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (i > 0)
            ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(static_cast<int>(i));
        changed |= DragFloatAxis("##v", &v[i], static_cast<int>(i), w, speed, min, max);
        ImGui::PopID();
    }
    return changed;
}

inline bool FieldDragFloat(const char* label, float* v, float speed = 1.0f, float min = 0.0f,
                           float max = 0.0f)
{
    return Field(label, [=] {
        return DragValue("##v", v, ImGui::GetContentRegionAvail().x, speed, min, max);
    });
}

inline bool FieldDragFloat3(const char* label, float* v, float speed = 1.0f)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    const std::span<float> xyz{v, 3};
#pragma clang diagnostic pop
    return Field(label, [=] { return DragFloatN(xyz, speed); });
}

inline bool DragGauge(float* v, float min, float max, float speed = 0.01f,
                      const char* fmt = "%.2f")
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight();
    const float t = ImSaturate((*v - min) / (max - min));

    // Before the drag, which runs with a transparent frame: the number stays
    // on top of its own gauge.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + w, p.y + h}, ImGui::GetColorU32(bg2), FrameRounding);
    if (t > 0.0f)
        dl->AddRectFilled(p, {p.x + w * t, p.y + h}, ImGui::GetColorU32(bg4), FrameRounding,
                          t < 1.0f ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersAll);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0, 0, 0, 0});
    const bool changed = DragValue("##v", v, w, speed, min, max, fmt);
    ImGui::PopStyleColor(3);
    return changed;
}

inline bool FieldSlider(const char* label, float* v, float min, float max, float speed = 0.01f)
{
    return Field(label, [=] { return DragGauge(v, min, max, speed); });
}

// ImGui centres a label inside the frame minus FramePadding, which pushes the
// glyph off centre in a button sized to its icon.
inline bool IconButton(const char* icon, float side)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2{0.5f, 0.5f});
    const bool pressed = ImGui::Button(icon, {side, side});
    ImGui::PopStyleVar(2);
    return pressed;
}

inline bool ColorField(const char* id, std::span<float> rgba)
{
    const ImVec4 col{rgba[0], rgba[1], rgba[2], 1.0f};
    const ImVec2 size{ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()};

    if (ImGui::ColorButton(id, col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                           size))
        ImGui::OpenPopup(id);

    bool changed = false;
    if (ImGui::BeginPopup(id))
    {
        changed = rgba.size() == 4 ? ImGui::ColorPicker4("##pick", rgba.data())
                                   : ImGui::ColorPicker3("##pick", rgba.data());
        ImGui::EndPopup();
    }
    return changed;
}

inline bool ComboField(const char* id, int* current, std::span<const char* const> items)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight();

    bool changed = false;
    ImGui::SetNextItemWidth(w);
    const char* preview = (*current >= 0 && static_cast<size_t>(*current) < items.size())
                              ? items[static_cast<size_t>(*current)]
                              : "";
    if (ImGui::BeginCombo(id, preview, ImGuiComboFlags_NoArrowButton))
    {
        for (size_t i = 0; i < items.size(); ++i)
            if (ImGui::Selectable(items[i], static_cast<size_t>(*current) == i))
            {
                *current = static_cast<int>(i);
                changed = true;
            }
        ImGui::EndCombo();
    }

    const float pad = ImGui::GetStyle().FramePadding.x;
    const float r = ImGui::GetTextLineHeight() * 0.21f;
    const float cx = p.x + w - pad - r;
    const float cy = p.y + h * 0.5f;
    ImGui::GetWindowDrawList()->AddTriangleFilled({cx - r, cy - r * 0.6f}, {cx + r, cy - r * 0.6f},
                                                  {cx, cy + r * 0.8f},
                                                  ImGui::GetColorU32(textDim));
    return changed;
}

inline bool ColorFieldRaw(const char* id, float* rgba, size_t count)
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
    const std::span<float> s{rgba, count};
#pragma clang diagnostic pop
    return ColorField(id, s);
}

inline bool AssetField(const char* icon, const char* name, ImVec4 iconColor)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight();
    const float pad = ImGui::GetStyle().FramePadding.x;

    const bool clicked = ImGui::InvisibleButton("##asset", {w, h});
    const ImU32 bg = ImGui::GetColorU32(ImGui::IsItemActive()    ? ImGuiCol_FrameBgActive
                                        : ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered
                                                                 : ImGuiCol_FrameBg);
    dl->AddRectFilled(p, {p.x + w, p.y + h}, bg, FrameRounding);

    float x = p.x + pad;
    const float textY = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    if (icon && icon[0] != 0)
    {
        dl->AddText({x, textY}, ImGui::GetColorU32(iconColor), icon);
        x += ImGui::CalcTextSize(icon).x + ImGui::GetStyle().ItemInnerSpacing.x;
    }

    const float chevron = ImGui::GetTextLineHeight() * 0.5f;
    const float nameEnd = p.x + w - pad - chevron - pad;
    ImGui::RenderTextEllipsis(dl, {x, textY}, {nameEnd, p.y + h}, nameEnd, name, nullptr, nullptr);

    const float cx = p.x + w - pad - chevron * 0.5f;
    const float cy = p.y + h * 0.5f;
    const float r = chevron * 0.42f;
    dl->AddTriangleFilled({cx - r, cy - r * 0.6f}, {cx + r, cy - r * 0.6f}, {cx, cy + r * 0.8f},
                          ImGui::GetColorU32(textDim));

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", name);
    return clicked;
}

}  // namespace batap::ui

