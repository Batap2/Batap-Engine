#include "BmatSerializer.h"

#include "Assets/Material.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace batap
{

bool writeBmat(const MaterialDesc& desc, const std::string& outPath)
{
    nlohmann::json j;
    j["albedo"]     = {desc.mat->albedo[0], desc.mat->albedo[1],
                       desc.mat->albedo[2], desc.mat->albedo[3]};
    j["roughness"]    = desc.mat->roughness;
    j["metallic"]     = desc.mat->metallic;
    j["reflectivity"] = desc.mat->reflectivity;

    if (!desc.albedoTexPath.empty())    j["albedoTex"]    = desc.albedoTexPath;
    if (!desc.normalTexPath.empty())    j["normalTex"]    = desc.normalTexPath;
    if (!desc.roughnessTexPath.empty()) j["roughnessTex"] = desc.roughnessTexPath;
    if (!desc.metallicTexPath.empty())  j["metallicTex"]  = desc.metallicTexPath;

    std::ofstream f(outPath);
    if (!f)
    {
        std::cerr << "[BmatSerializer] Cannot open for write: " << outPath << "\n";
        return false;
    }
    f << j.dump(4);
    return true;
}

bool writeBmat(const Material& mat, const std::string& outPath)
{
    MaterialDesc desc;
    desc.mat = const_cast<Material*>(&mat);
    return writeBmat(desc, outPath);
}

std::optional<MaterialFileData> readBmat(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
    {
        std::cerr << "[BmatSerializer] Cannot open: " << path << "\n";
        return std::nullopt;
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(f);
    }
    catch (const nlohmann::json::exception& e)
    {
        std::cerr << "[BmatSerializer] Parse error in " << path << ": " << e.what() << "\n";
        return std::nullopt;
    }

    MaterialFileData data;

    if (j.contains("albedo") && j["albedo"].is_array() && j["albedo"].size() == 4)
    {
        const auto& a     = j["albedo"];
        data.mat.albedo[0] = a[0].get<float>();
        data.mat.albedo[1] = a[1].get<float>();
        data.mat.albedo[2] = a[2].get<float>();
        data.mat.albedo[3] = a[3].get<float>();
    }
    if (j.contains("roughness"))    data.mat.roughness    = j["roughness"].get<float>();
    if (j.contains("metallic"))     data.mat.metallic     = j["metallic"].get<float>();
    if (j.contains("reflectivity")) data.mat.reflectivity = j["reflectivity"].get<float>();

    if (j.contains("albedoTex"))    data.albedoTexPath    = j["albedoTex"].get<std::string>();
    if (j.contains("normalTex"))    data.normalTexPath    = j["normalTex"].get<std::string>();
    if (j.contains("roughnessTex")) data.roughnessTexPath = j["roughnessTex"].get<std::string>();
    if (j.contains("metallicTex"))  data.metallicTexPath  = j["metallicTex"].get<std::string>();

    return data;
}

}  // namespace batap
