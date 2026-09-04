#pragma once

#include <optional>
#include <string>

#include "Shaders/ShaderInterop.h"

namespace batap
{

// Texture paths are relative to the asset base dir.
// Empty string = no texture assigned.
struct MaterialDesc
{
    Material*   mat = nullptr; // non-owning, must outlive the desc
    std::string albedoTexPath;
    std::string normalTexPath;
    std::string roughnessTexPath;
    std::string metallicTexPath;
};

struct MaterialFileData
{
    Material    mat;
    std::string albedoTexPath;
    std::string normalTexPath;
    std::string roughnessTexPath;
    std::string metallicTexPath;
};

// Write with texture paths
bool writeBmat(const MaterialDesc& desc, const std::string& outPath);

// Write without texture paths (backwards compatible)
bool writeBmat(const Material& mat, const std::string& outPath);

// Read — returns material data + texture paths
std::optional<MaterialFileData> readBmat(const std::string& path);

}  // namespace batap
