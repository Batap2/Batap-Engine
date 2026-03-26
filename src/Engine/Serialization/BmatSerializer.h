#pragma once

#include <optional>
#include <string>

namespace batap
{

struct Material;

bool writeBmat(const Material& mat, const std::string& outPath);
std::optional<Material> readBmat(const std::string& path);

}  // namespace batap
