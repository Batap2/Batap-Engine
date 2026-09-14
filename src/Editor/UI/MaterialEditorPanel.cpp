#include "UI/MaterialEditorPanel.h"

#include "App.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetSlotMap.h"
#include "Assets/Texture.h"
#include "Engine.h"
#include "Shaders/ShaderInterop.h"
#include "UI/AssetRow.h"
#include "UI/Field.h"

#include <imgui.h>

#include <array>
#include <filesystem>
#include <string>

namespace batap
{
namespace
{
constexpr const char* kWindowTitle = "Material Editor";

// A material always holds a valid texture index; the default white map is the
// neutral multiplier, so it reads as "no texture" rather than as a pick.
std::string texLabelFromBindlessIndex(AssetManager& am, uint32_t bindlessIndex)
{
    auto* white = am.get<Texture>(std::string("__default_white"));
    if (white && bindlessIndex == white->bindlessIndex_)
        return {};
    std::string result;
    am.getSlotMap<Texture>()->for_each(
        [&](TextureHandle, const AssetSlotMap<Texture>::Asset& a)
        {
            if (result.empty() && a.value_.bindlessIndex_ == bindlessIndex)
                result = std::filesystem::path(a.path_).stem().string();
        });
    return result;
}

void drawParameters(App& app, MaterialHandle handle, const Material& mat,
                    AssetPickerPopup& picker)
{
    Material copy = mat;
    bool changed = false;

    if (auto f = ui::BeginFields("matparams"))
    {
        changed |= ui::Field("Albedo", [&] { return ui::ColorField("##alb", copy.albedo, 4); });
        changed |= ui::FieldSlider("Roughness", &copy.roughness, 0.f, 1.f);
        changed |= ui::FieldSlider("Metallic", &copy.metallic, 0.f, 1.f);
        changed |= ui::FieldSlider("Reflectivity", &copy.reflectivity, 0.f, 1.f);
        changed |= ui::Field("Shading",
                             [&]
                             {
                                 static constexpr std::array<const char*, 2> kModels = {"Lit",
                                                                                        "Unlit"};
                                 int current = static_cast<int>(copy.shadingModel_);
                                 if (!ui::ComboField("##shading", &current, kModels))
                                     return false;
                                 copy.shadingModel_ = static_cast<uint32_t>(current);
                                 return true;
                             });
    }

    if (changed)
        app.ctx_->assetManager_->update(handle, copy);

    ImGui::Spacing();
    ImGui::SeparatorText("Textures");

    static constexpr std::array<const char*, 4> kTexLabels = {"Albedo", "Normal", "Roughness",
                                                              "Metallic"};
    const std::array<uint32_t, 4> texIdx = {mat.albedoTexIdx_, mat.normalTexIdx_,
                                            mat.roughnessTexIdx_, mat.metallicTexIdx_};

    if (auto f = ui::BeginFields("mattex"))
    {
        for (uint8_t ch = 0; ch < 4; ++ch)
        {
            ui::Field(kTexLabels[ch],
                      [&, ch]
                      {
                          const std::string name =
                              texLabelFromBindlessIndex(*app.ctx_->assetManager_, texIdx[ch]);
                          if (ui::AssetRow(AssetType::Texture, name))
                              picker.open(handle, ch, app.projectDir_);
                      });
        }
    }
}
}  // namespace

void MaterialEditorPanel::open(MaterialHandle mat)
{
    material_ = mat;
    visible_ = true;
    ImGui::SetWindowFocus(kWindowTitle);
}

void MaterialEditorPanel::draw(App& app)
{
    if (!visible_)
        return;

    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowTitle, &visible_))
    {
        AssetManager& am = *app.ctx_->assetManager_;
        const Material* mat = material_ ? am.get<Material>(material_) : nullptr;
        if (!mat)
        {
            ImGui::TextDisabled("No material selected");
        }
        else
        {
            const std::string* path = am.getPath(material_);
            const std::string name =
                path ? std::filesystem::path(*path).stem().string() : std::string("unnamed");
            ImGui::SeparatorText(name.c_str());

            drawParameters(app, material_, *mat, assetPicker_);
        }

        assetPicker_.draw(app);
    }
    ImGui::End();
}
}  // namespace batap
