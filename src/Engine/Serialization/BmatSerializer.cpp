#include "BmatSerializer.h"

#include "Assets/Material.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace batap
{

bool writeBmat(const Material& mat, const std::string& outPath)
{
    nlohmann::json j;
    j["albedo"] = {mat.albedo[0], mat.albedo[1], mat.albedo[2], mat.albedo[3]};
    j["roughness"] = mat.roughness;
    j["metallic"] = mat.metallic;

    std::ofstream f(outPath);
    if (!f)
    {
        std::cerr << "[BmatSerializer] Cannot open for write: " << outPath << "\n";
        return false;
    }
    f << j.dump(4);
    return true;
}

std::optional<Material> readBmat(const std::string& path)
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

    Material mat;
    if (j.contains("albedo") && j["albedo"].is_array() && j["albedo"].size() == 4)
    {
        const auto& a = j["albedo"];
        mat.albedo[0] = a[0].get<float>();
        mat.albedo[1] = a[1].get<float>();
        mat.albedo[2] = a[2].get<float>();
        mat.albedo[3] = a[3].get<float>();
    }
    if (j.contains("roughness"))
        mat.roughness = j["roughness"].get<float>();
    if (j.contains("metallic"))
        mat.metallic = j["metallic"].get<float>();

    return mat;
}

}  // namespace batap
