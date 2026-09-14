#pragma once

#include "Assets/AssetHandle.h"
#include "Components/EntityHandle.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace batap
{

struct App;
struct Engine;
struct ComponentType;
struct Field;

struct AssetPickerPopup
{
    void open(EntityHandle ent, AssetType type, const std::string& projectDir,
              uint8_t slotIndex = 0);
    // Open specifically for picking a texture channel on a material.
    // channel: 0=albedo 1=normal 2=roughness 3=metallic
    void open(MaterialHandle mat, uint8_t channel, const std::string& projectDir);
    // Open specifically for picking an HDRI for a Skybox_C component.
    void openHdri(EntityHandle ent, const std::string& projectDir);
    void openField(EntityHandle ent, const ComponentType& component, const Field& field,
                   AssetType type, const std::string& projectDir);
    // True on the frame a pick or a clear was applied.
    bool draw(App& app);

  private:
    // Loads the asset at that path and assigns it to whatever opened the
    // picker. Shared by picking an existing file and creating a new one.
    bool applyPath(App& app, const std::filesystem::path& path);
    // Refills entries_ from projectDir_ + exts_, leaving the pick target alone.
    void rescan();

    struct Entry { std::string name; std::filesystem::path path; };

    bool           pendingOpen_  = false;
    bool           isHdriPick_   = false;
    EntityHandle   ent_{};
    AssetType      type_{AssetType::Mesh};
    uint8_t        slotIndex_  = 0;
    MaterialHandle matHandle_  = {};
    uint8_t        texChannel_ = 0;  // 0=albedo 1=normal 2=roughness 3=metallic
    // Field target: looked up by name and offset at apply time, so a hot
    // reload between opening and picking cannot leave a dangling pointer.
    std::string fieldComponent_;
    size_t      fieldOffset_ = 0;
    bool        isFieldPick_ = false;
    std::string        search_;
    std::string              projectDir_;
    std::vector<std::string> exts_;
    std::vector<Entry>       entries_;
};

}  // namespace batap
