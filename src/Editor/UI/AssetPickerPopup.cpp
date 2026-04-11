#include "AssetPickerPopup.h"

#include "App.h"
#include "Assets/AssetHandle.h"
#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Assets/Texture.h"
#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"

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
        case AssetType::Material:
            return ".bmat";
    }
    return {};
}

void AssetPickerPopup::open(EntityHandle ent, AssetType type, const std::string& projectDir,
                            uint8_t slotIndex)
{
    ent_        = ent;
    type_       = type;
    slotIndex_  = slotIndex;
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

    pendingOpen_ = true;
}

void AssetPickerPopup::open(MaterialHandle mat, uint8_t channel, const std::string& projectDir)
{
    ent_        = {};
    type_       = AssetType::Texture;
    matHandle_  = mat;
    texChannel_ = channel;
    search_.clear();
    entries_.clear();

    if (projectDir.empty())
        return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(projectDir))
    {
        if (!entry.is_regular_file())
            continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".btex")
            continue;
        entries_.push_back({entry.path().stem().string(), entry.path()});
    }
    pendingOpen_ = true;
}

static void applyTexture(App& app, MaterialHandle matHandle, uint8_t channel, uint32_t heapIdx)
{
    auto* mat = app.ctx_->_assetManager->get<Material>(matHandle);
    if (!mat)
        return;
    Material copy = *mat;
    switch (channel)
    {
        case 0: copy.albedoTexIdx_    = heapIdx; break;
        case 1: copy.normalTexIdx_    = heapIdx; break;
        case 2: copy.roughnessTexIdx_ = heapIdx; break;
        case 3: copy.metallicTexIdx_  = heapIdx; break;
    }
    app.ctx_->_assetManager->update(matHandle, copy);
}

void AssetPickerPopup::draw(App& app)
{
    if (pendingOpen_)
    {
        ImGui::OpenPopup(kId);
        pendingOpen_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(300, 360), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup(kId))
        return;

    const char* title = (type_ == AssetType::Mesh)       ? "Select Mesh"
                        : (type_ == AssetType::Texture)  ? "Select Texture"
                        : (type_ == AssetType::Material) ? "Select Material"
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
            const auto relPath = std::filesystem::relative(e.path, app.projectDir_).string();
            auto handle = loadAsset(relPath, *app.ctx_);
            if (handle)
            {
                if (type_ == AssetType::Mesh)
                    if (auto* meshC = ent_.try_get<Mesh_C>())
                        meshC->_mesh = std::get<MeshHandle>(*handle);

                if (type_ == AssetType::Material)
                    if (auto* mc = ent_.try_get<Materials_C>())
                        if (slotIndex_ < mc->count)
                            mc->slots[slotIndex_] = std::get<MaterialHandle>(*handle);

                if (type_ == AssetType::Texture && matHandle_)
                    if (auto* th = std::get_if<TextureHandle>(&*handle))
                        if (auto* tex = app.ctx_->_assetManager->get<Texture>(*th))
                            applyTexture(app, matHandle_, texChannel_, tex->heapIdx_);
            }

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

        if (type_ == AssetType::Material)
            if (auto* mc = ent_.try_get<Materials_C>())
                if (slotIndex_ < mc->count)
                    mc->slots[slotIndex_] = MaterialHandle::null();

        if (type_ == AssetType::Texture && matHandle_)
            if (auto* white = app.ctx_->_assetManager->get<Texture>(std::string("__default_white")))
                applyTexture(app, matHandle_, texChannel_, white->heapIdx_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

}  // namespace batap
