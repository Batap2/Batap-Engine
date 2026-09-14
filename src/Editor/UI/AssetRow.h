#pragma once

#include "Assets/AssetHandle.h"
#include "Assets/AssetManager.h"
#include "UI/Field.h"
#include "UI/IconsMaterialDesign.h"

#include <filesystem>
#include <string>

namespace batap::ui
{
// Stem of the asset's path, empty when the handle is unset or no longer known.
template <class T>
std::string assetName(AssetManager& assets, AssetHandle<T> handle)
{
    if (!handle)
        return {};
    const std::string* path = assets.getPath(handle);
    return path ? std::filesystem::path(*path).stem().string() : std::string{};
}

inline const char* assetIcon(AssetType type)
{
    switch (type)
    {
        case AssetType::Texture:  return ICON_MD_IMAGE;
        case AssetType::Material: return ICON_MD_PALETTE;
        case AssetType::Mesh:     break;
    }
    return ICON_MD_VIEW_IN_AR;
}

inline ComponentColor assetColor(AssetType type)
{
    switch (type)
    {
        case AssetType::Texture:  return ComponentColor::Magenta;
        case AssetType::Material: return ComponentColor::Yellow;
        case AssetType::Mesh:     break;
    }
    return ComponentColor::Violet;
}

inline bool AssetRow(const char* icon, ComponentColor color, const std::string& name)
{
    return AssetField(icon, name.empty() ? "None" : name.c_str(), colorOf(color));
}

inline bool AssetRow(AssetType type, const std::string& name)
{
    return AssetRow(assetIcon(type), assetColor(type), name);
}

}  // namespace batap::ui
