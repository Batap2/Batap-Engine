#pragma once

#include "AssetHandle.h"

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace batap
{

// Unlit variant of the default material, created with it at engine init.
inline constexpr const char* kUnlitMaterialPath = "__unlit_material";

inline bool isBuiltinAsset(std::string_view path)
{
    return path.starts_with("__");
}

struct Engine;
struct AssetManager;

// Loads an asset from disk into memory and registers it in the AssetManager.
// Supported formats:
//   .bmesh              → Mesh
//   .bmat               → Material
//   .btex               → Texture (descriptor file)
//   .png / .jpg / .jpeg → Texture (raw image)
// Returns nullopt if the format is unsupported or loading fails.
// Game code reaches the AssetManager through World::assets(), never the Engine.
std::optional<AssetHandleAny> loadAsset(std::string_view path, AssetManager& assets);
std::optional<AssetHandleAny> loadAsset(std::string_view path, const Engine& ctx);

// Null handle if loading fails or the file is not a T.
template <class T>
AssetHandle<T> loadAsset(std::string_view path, AssetManager& assets)
{
    auto any = loadAsset(path, assets);
    if (!any)
        return {};
    if (auto* handle = std::get_if<AssetHandle<T>>(&*any))
        return *handle;
    return {};
}

template <class T>
AssetHandle<T> loadAsset(std::string_view path, const Engine& ctx)
{
    auto any = loadAsset(path, ctx);
    if (!any)
        return {};
    if (auto* handle = std::get_if<AssetHandle<T>>(&*any))
        return *handle;
    return {};
}

// Re-reads an asset already in memory and swaps its content in place: the handle
// stays valid, so every component pointing at it shows the new data. Returns
// false if the path is not loaded or the file cannot be read — the content in
// memory is then left untouched.
bool reloadAsset(std::string_view path, AssetManager& assets);
bool reloadAsset(std::string_view path, const Engine& ctx);

// Creates engine built-in assets: 1×1 white texture + default material (GPU slot 0).
// Must be called once, before any scene assets are loaded.
void createDefaultAssets(const Engine& ctx);

}  // namespace batap
