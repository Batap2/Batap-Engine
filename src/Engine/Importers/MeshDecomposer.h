#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace batap
{

// Every path is relative to baseDir — the key an asset is addressed by. A file
// written outside the project has no key and is not reported.
struct DecomposeResult
{
    std::vector<std::string> bmeshPaths;
    std::vector<std::string> bmatPaths;
    std::vector<std::string> texturePaths;
    std::vector<std::string> btplPaths;
    bool ok = false;
};

DecomposeResult decomposeSourceFile(std::string_view sourcePath, std::string_view outputDir,
                                    std::string_view baseDir);

}  // namespace batap
