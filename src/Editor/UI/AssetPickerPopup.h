#pragma once

#include "Assets/AssetHandle.h"
#include "Components/EntityHandle.h"

namespace batap
{
struct App;

struct AssetPickerPopup
{
    static constexpr const char* kId = "##AssetPicker";

    void open(EntityHandle ent, AssetType type);
    void draw(App& app);

  private:
    EntityHandle ent_{};
    AssetType    type_{AssetType::Mesh};
    char         search_[128] = {};
};
}  // namespace batap
