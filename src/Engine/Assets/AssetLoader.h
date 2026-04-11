#pragma once

#include "AssetHandle.h"

#include <optional>
#include <string_view>

namespace batap
{

struct Context;

// Loads an asset from disk into memory and registers it in the AssetManager.
// Supported formats:
//   .bmesh              → Mesh
//   .bmat               → Material
//   .btex               → Texture (descriptor file)
//   .png / .jpg / .jpeg → Texture (raw image)
// Returns nullopt if the format is unsupported or loading fails.
std::optional<AssetHandleAny> loadAsset(std::string_view path, const Context& ctx);

// Creates engine built-in assets: 1×1 white texture + default material (GPU slot 0).
// Must be called once, before any scene assets are loaded.
void createDefaultAssets(const Context& ctx);

}  // namespace batap
