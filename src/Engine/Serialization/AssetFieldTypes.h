#pragma once

namespace batap
{
// Fills toJson/fromJson for MeshHandle, TextureHandle and MaterialHandle.
// A handle is a session-local index, so it is stored as the asset path the
// AssetManager knows it by, and resolved back (loading on demand) at read.
// Called once by the Engine ctor, before the registry is validated.
void registerAssetFieldTypes();
}  // namespace batap
