#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace batap
{

struct ImportOptions
{
    std::string outputDir;  // where to write engine-native files (.bmesh, .btpl, ...)
};

struct ImportResult
{
    enum class Kind
    {
        Unsupported,
        Decomposed,
        Failed
    };

    Kind                     kind = Kind::Unsupported;
    std::vector<std::string> writtenFiles;
    std::string              message;

    explicit operator bool() const { return kind == Kind::Decomposed; }
};

// Converts external source files into engine-native formats.
// Does NOT load anything into memory or touch the world.
// outputDir must be set; returns Unsupported if the format is not handled.
ImportResult importFile(std::string_view path, ImportOptions opts);

}  // namespace batap
