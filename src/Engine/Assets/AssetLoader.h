#pragma once

#include "AssetHandle.h"

#include <optional>
#include <string_view>

namespace batap
{

struct Context;

// Loads an asset from disk into memory and registers it in the AssetManager.
// Supported formats:
//   .bmesh              → Mesh    (GPU vertex/index buffers)
//   .png / .jpg / .jpeg → Texture (GPU texture — not yet implemented)
// Returns nullopt if the format is unsupported or loading fails.
std::optional<AssetHandleAny> loadAsset(std::string_view path, const Context& ctx);

}  // namespace batap
