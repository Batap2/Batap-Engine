#include "AssetPickerPopup.h"

#include "App.h"
#include "Assets/AssetHandle.h"
#include "Assets/AssetLoader.h"
#include "Components/Mesh_C.h"
#include "Context.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <filesystem>

namespace batap
{

static constexpr const char* kId = "##AssetPicker";

static std::string_view extensionFor(AssetType type)
{
    switch (type)
    {
        case AssetType::Mesh:
            return ".bmesh";
        case AssetType::Texture:
            return ".png";  // TODO: also .jpg/.jpeg
    }
    return {};
}

void AssetPickerPopup::open(EntityHandle ent, AssetType type, const std::string& projectDir)
{
    ent_ = ent;
    type_ = type;
    search_.clear();
    entries_.clear();

    if (projectDir.empty())
        return;

    const auto ext = extensionFor(type);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(projectDir))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ext)
            continue;
        entries_.push_back({entry.path().stem().string(), entry.path()});
    }

    ImGui::OpenPopup(kId);
}

void AssetPickerPopup::draw(App& app)
{
    ImGui::SetNextWindowSize(ImVec2(300, 360), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup(kId))
        return;

    const char* title = (type_ == AssetType::Mesh)      ? "Select Mesh"
                        : (type_ == AssetType::Texture) ? "Select Texture"
                                                        : "Select Asset";
    ImGui::SeparatorText(title);

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Search...", &search_);
    ImGui::Separator();

    ImGui::BeginChild("##list", ImVec2(0, 220), true);

    for (const auto& e : entries_)
    {
        if (search_[0] != '\0' && e.name.find(search_) == std::string::npos)
            continue;

        if (ImGui::Selectable(e.name.c_str()))
        {
            auto handle = loadAsset(e.path.string(), *app.ctx_);
            if (handle && type_ == AssetType::Mesh)
                if (auto* meshC = ent_.try_get<Mesh_C>())
                    meshC->_mesh = std::get<MeshHandle>(*handle);

            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Clear"))
    {
        if (type_ == AssetType::Mesh)
            if (auto* meshC = ent_.try_get<Mesh_C>())
                meshC->_mesh = MeshHandle::null();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

}  // namespace batap
