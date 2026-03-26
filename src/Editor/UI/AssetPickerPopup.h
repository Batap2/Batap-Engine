#pragma once

#include "Assets/AssetHandle.h"
#include "Components/EntityHandle.h"

#include <filesystem>
#include <string>
#include <vector>

namespace batap
{

struct App;
struct Context;

struct AssetPickerPopup
{
    void open(EntityHandle ent, AssetType type, const std::string& projectDir,
              uint8_t slotIndex = 0);
    void draw(App& app);

  private:
    struct Entry { std::string name; std::filesystem::path path; };

    EntityHandle ent_{};
    AssetType    type_{AssetType::Mesh};
    uint8_t      slotIndex_ = 0;
    std::string     search_;
    std::vector<Entry> entries_;
};

}  // namespace batap
