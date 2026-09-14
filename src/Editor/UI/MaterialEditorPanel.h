#pragma once

#include "Assets/AssetHandle.h"
#include "UI/AssetPickerPopup.h"

namespace batap
{
struct App;

struct MaterialEditorPanel
{
    void open(MaterialHandle mat);
    void draw(App& app);

   private:
    MaterialHandle material_{};
    bool visible_ = false;
    AssetPickerPopup assetPicker_;
};
}  // namespace batap
