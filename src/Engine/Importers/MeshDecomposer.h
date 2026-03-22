#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace batap
{

struct DecomposeResult
{
    std::vector<std::string> bmeshPaths;
    std::vector<std::string> texturePaths;
    std::string btplPath;
    bool ok = false;
};

DecomposeResult decomposeSourceFile(std::string_view sourcePath, std::string_view outputDir);

}  // namespace batap
