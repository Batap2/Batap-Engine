#include "BtexSerializer.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace batap
{

namespace
{

std::string colorSpaceToString(TextureColorSpace cs)
{
    switch (cs)
    {
        case TextureColorSpace::SRGB:   return "srgb";
        case TextureColorSpace::Linear: return "linear";
    }
    return "srgb";
}

std::string filterToString(TextureFilter f)
{
    switch (f)
    {
        case TextureFilter::Linear:      return "linear";
        case TextureFilter::Nearest:     return "nearest";
        case TextureFilter::Anisotropic: return "anisotropic";
    }
    return "linear";
}

std::string wrapToString(TextureWrap w)
{
    switch (w)
    {
        case TextureWrap::Repeat: return "repeat";
        case TextureWrap::Clamp:  return "clamp";
        case TextureWrap::Mirror: return "mirror";
    }
    return "repeat";
}

TextureColorSpace colorSpaceFromString(const std::string& s)
{
    if (s == "linear") return TextureColorSpace::Linear;
    return TextureColorSpace::SRGB;
}

TextureFilter filterFromString(const std::string& s)
{
    if (s == "nearest")     return TextureFilter::Nearest;
    if (s == "anisotropic") return TextureFilter::Anisotropic;
    return TextureFilter::Linear;
}

TextureWrap wrapFromString(const std::string& s)
{
    if (s == "clamp")  return TextureWrap::Clamp;
    if (s == "mirror") return TextureWrap::Mirror;
    return TextureWrap::Repeat;
}

}  // namespace

bool writeBtex(const TextureDesc& desc, const std::string& outPath)
{
    nlohmann::json j;
    j["source"]     = desc.sourcePath;
    j["colorSpace"] = colorSpaceToString(desc.colorSpace);
    j["filter"]     = filterToString(desc.filter);
    j["wrapU"]      = wrapToString(desc.wrapU);
    j["wrapV"]      = wrapToString(desc.wrapV);
    j["mipLevels"]  = desc.mipLevels;

    std::ofstream f(outPath);
    if (!f)
    {
        std::cerr << "[BtexSerializer] Cannot open for write: " << outPath << "\n";
        return false;
    }
    f << j.dump(4);
    return true;
}

bool writeBtex(const Texture& tex, const std::string& sourcePath, const std::string& outPath)
{
    TextureDesc desc;
    desc.sourcePath = sourcePath;
    desc.colorSpace = tex.colorSpace_;
    desc.filter     = tex.filter_;
    desc.wrapU      = tex.wrapU_;
    desc.wrapV      = tex.wrapV_;
    desc.mipLevels  = tex.mipLevels_;
    return writeBtex(desc, outPath);
}

std::optional<TextureDesc> readBtex(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
    {
        std::cerr << "[BtexSerializer] Cannot open: " << path << "\n";
        return std::nullopt;
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(f);
    }
    catch (const nlohmann::json::exception& e)
    {
        std::cerr << "[BtexSerializer] Parse error in " << path << ": " << e.what() << "\n";
        return std::nullopt;
    }

    TextureDesc desc;
    if (j.contains("source"))     desc.sourcePath = j["source"].get<std::string>();
    if (j.contains("colorSpace")) desc.colorSpace = colorSpaceFromString(j["colorSpace"].get<std::string>());
    if (j.contains("filter"))     desc.filter     = filterFromString(j["filter"].get<std::string>());
    if (j.contains("wrapU"))      desc.wrapU      = wrapFromString(j["wrapU"].get<std::string>());
    if (j.contains("wrapV"))      desc.wrapV      = wrapFromString(j["wrapV"].get<std::string>());
    if (j.contains("mipLevels"))  desc.mipLevels  = j["mipLevels"].get<uint32_t>();

    return desc;
}

}  // namespace batap
