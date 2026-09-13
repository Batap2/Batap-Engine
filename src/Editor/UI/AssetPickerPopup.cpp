#include "AssetPickerPopup.h"

#include "App.h"
#include "Assets/AssetHandle.h"
#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "Shaders/ShaderInterop.h"
#include "Assets/Texture.h"
#include "Components/Materials_C.h"
#include "Components/Mesh_C.h"
#include "Components/Skybox_C.h"
#include "Instance/InstanceManager.h"
#include "Reflection/ComponentRegistry.h"
#include "World.h"

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
            return ".png";
        case AssetType::Material:
            return ".bmat";
    }
    return {};
}

void AssetPickerPopup::open(EntityHandle ent, AssetType type, const std::string& projectDir,
                            uint8_t slotIndex)
{
    ent_ = ent;
    type_ = type;
    slotIndex_ = slotIndex;
    isHdriPick_ = false;
    isFieldPick_ = false;
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

void AssetPickerPopup::openHdri(EntityHandle ent, const std::string& projectDir)
{
    ent_ = ent;
    isHdriPick_ = true;
    isFieldPick_ = false;
    search_.clear();
    entries_.clear();

    if (projectDir.empty())
        return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(projectDir))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".hdr")
            continue;
        entries_.push_back({entry.path().stem().string(), entry.path()});
    }
    pendingOpen_ = true;
}

void AssetPickerPopup::openField(EntityHandle ent, const ComponentType& component,
                                 const Field& field, AssetType type,
                                 const std::string& projectDir)
{
    ent_ = ent;
    type_ = type;
    matHandle_ = {};
    isHdriPick_ = false;
    isFieldPick_ = true;
    fieldComponent_ = component.name;
    fieldOffset_ = field.offset;
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
    ent_ = {};
    type_ = AssetType::Texture;
    matHandle_ = mat;
    texChannel_ = channel;
    isHdriPick_ = false;
    isFieldPick_ = false;
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

static void applyTexture(App& app, MaterialHandle matHandle, uint8_t channel, uint32_t bindlessIndex)
{
    auto* mat = app.ctx_->assetManager_->get<Material>(matHandle);
    if (!mat)
        return;
    Material copy = *mat;
    switch (channel)
    {
        case 0:
            copy.albedoTexIdx_ = bindlessIndex;
            break;
        case 1:
            copy.normalTexIdx_ = bindlessIndex;
            break;
        case 2:
            copy.roughnessTexIdx_ = bindlessIndex;
            break;
        case 3:
            copy.metallicTexIdx_ = bindlessIndex;
            break;
    }
    app.ctx_->assetManager_->update(matHandle, copy);
}

namespace
{
bool applyToField(App& app, EntityHandle ent, const std::string& componentName, size_t offset,
                  AssetType type, const AssetHandleAny* picked)
{
    const ComponentType* ct = ComponentRegistry::instance().find(componentName);
    if (!ct || !ct->tryGet || !ent.valid())
        return false;

    void* component = ct->tryGet(*ent.reg_, ent.entity_);
    if (!component)
        return false;

    const Field* field = nullptr;
    for (const Field& f : ct->fields)
        if (f.offset == offset)
            field = &f;
    if (!field)
        return false;

    void* dst = field->ptrIn(component);
    switch (type)
    {
        case AssetType::Mesh:
            *static_cast<MeshHandle*>(dst) =
                picked ? std::get<MeshHandle>(*picked) : MeshHandle::null();
            break;
        case AssetType::Texture:
            *static_cast<TextureHandle*>(dst) =
                picked ? std::get<TextureHandle>(*picked) : TextureHandle::null();
            break;
        case AssetType::Material:
            *static_cast<MaterialHandle*>(dst) =
                picked ? std::get<MaterialHandle>(*picked) : MaterialHandle::null();
            break;
    }

    if (ct->patch)
        ct->patch(*ent.reg_, ent.entity_);
    app.world_->instances().markDirty(ent, ct->mask());
    return true;
}
}  // namespace

bool AssetPickerPopup::draw(App& app)
{
    if (pendingOpen_)
    {
        ImGui::OpenPopup(kId);
        pendingOpen_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(300, 360), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup(kId))
        return false;

    bool applied = false;

    const char* title = isHdriPick_                      ? "Select HDRI"
                        : (type_ == AssetType::Mesh)     ? "Select Mesh"
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
            if (handle && isFieldPick_)
                applied = applyToField(app, ent_, fieldComponent_, fieldOffset_, type_,
                                       &*handle);
            else if (handle)
            {
                if (type_ == AssetType::Mesh)
                    if (auto* meshC = ent_.try_get<Mesh_C>())
                        meshC->mesh_ = std::get<MeshHandle>(*handle);

                if (type_ == AssetType::Material)
                    if (auto* mc = ent_.try_get<Materials_C>())
                        if (slotIndex_ < mc->slots.size())
                        {
                            mc->slots[slotIndex_] = std::get<MaterialHandle>(*handle);
                            app.world_->instances().markDirty<Materials_C>(ent_);
                        }

                if (type_ == AssetType::Texture && matHandle_)
                    if (auto* th = std::get_if<TextureHandle>(&*handle))
                        if (auto* tex = app.ctx_->assetManager_->get<Texture>(*th))
                            applyTexture(app, matHandle_, texChannel_, tex->bindlessIndex_);

                if (isHdriPick_)
                    if (auto* sky = ent_.try_get<Skybox_C>())
                        if (auto* th = std::get_if<TextureHandle>(&*handle))
                        {
                            sky->hdri_ = *th;
                            app.world_->instances().markDirty<Skybox_C>(ent_);
                        }
            }

            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Clear"))
    {
        if (isFieldPick_)
            applied = applyToField(app, ent_, fieldComponent_, fieldOffset_, type_, nullptr);

        if (!isFieldPick_ && type_ == AssetType::Mesh)
            if (auto* meshC = ent_.try_get<Mesh_C>())
                meshC->mesh_ = MeshHandle::null();

        if (!isFieldPick_ && type_ == AssetType::Material)
            if (auto* mc = ent_.try_get<Materials_C>())
                if (slotIndex_ < mc->slots.size())
                    mc->slots[slotIndex_] = MaterialHandle::null();

        if (type_ == AssetType::Texture && matHandle_)
        {
            const std::string defaultKey = (texChannel_ == 1) ? "__default_flat_normal"
                                                              : "__default_white";
            if (auto* def = app.ctx_->assetManager_->get<Texture>(defaultKey))
                applyTexture(app, matHandle_, texChannel_, def->bindlessIndex_);
        }

        if (isHdriPick_)
            if (auto* sky = ent_.try_get<Skybox_C>())
                sky->hdri_ = TextureHandle::null();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
    return applied;
}

}  // namespace batap
