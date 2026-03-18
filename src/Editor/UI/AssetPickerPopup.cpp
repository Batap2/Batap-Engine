#include "AssetPickerPopup.h"

#include "App.h"
#include "Assets/AssetManager.h"
#include "Components/Mesh_C.h"

#include <imgui.h>

namespace batap
{

void AssetPickerPopup::open(EntityHandle ent, AssetType type)
{
    ent_    = ent;
    type_   = type;
    search_[0] = '\0';
    ImGui::OpenPopup(kId);
}

void AssetPickerPopup::draw(App& app)
{
    ImGui::SetNextWindowSize(ImVec2(300, 360), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup(kId))
        return;

    const char* title = (type_ == AssetType::Mesh)      ? "Select Mesh"
                      : (type_ == AssetType::Texture)   ? "Select Texture"
                                                        : "Select Asset";
    ImGui::SeparatorText(title);

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Search...", search_, sizeof(search_));
    ImGui::Separator();

    ImGui::BeginChild("##list", ImVec2(0, 220), true);

    app.assetManager_->forEachAssetOfType(
        type_,
        [&](AssetHandleAny hAny, std::string_view name, std::string_view /*path*/)
        {
            if (search_[0] != '\0' && name.find(search_) == std::string_view::npos)
                return;

            std::string label(name);
            if (ImGui::Selectable(label.c_str()))
            {
                if (type_ == AssetType::Mesh)
                {
                    if (auto* meshC = ent_.try_get<Mesh_C>())
                        meshC->_mesh = std::get<MeshHandle>(hAny);
                }
                ImGui::CloseCurrentPopup();
            }
        });

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
