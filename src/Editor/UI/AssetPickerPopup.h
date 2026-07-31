#pragma once

#include "Assets/AssetHandle.h"
#include "Components/EntityHandle.h"

#include <filesystem>
#include <string>
#include <vector>

namespace batap
{

struct App;
struct Engine;

struct AssetPickerPopup
{
    void open(EntityHandle ent, AssetType type, const std::string& projectDir,
              uint8_t slotIndex = 0);
    // Open specifically for picking a texture channel on a material.
    // channel: 0=albedo 1=normal 2=roughness 3=metallic
    void open(MaterialHandle mat, uint8_t channel, const std::string& projectDir);
    // Open specifically for picking an HDRI for a Skybox_C component.
    void openHdri(EntityHandle ent, const std::string& projectDir);
    void draw(App& app);

  private:
    struct Entry { std::string name; std::filesystem::path path; };

    bool           pendingOpen_  = false;
    bool           isHdriPick_   = false;
    EntityHandle   ent_{};
    AssetType      type_{AssetType::Mesh};
    uint8_t        slotIndex_  = 0;
    MaterialHandle matHandle_  = {};
    uint8_t        texChannel_ = 0;  // 0=albedo 1=normal 2=roughness 3=metallic
    std::string        search_;
    std::vector<Entry> entries_;
};

}  // namespace batap
