#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace batap
{

struct AssetManager;

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

    Kind kind = Kind::Unsupported;
    // Relative to the project — the key an asset is addressed by.
    std::vector<std::string> writtenFiles;
    std::string message;

    explicit operator bool() const { return kind == Kind::Decomposed; }
};

// Converts external source files into engine-native formats.
// Does NOT load anything into memory or touch the world.
// outputDir must be set; returns Unsupported if the format is not handled.
ImportResult importFile(std::string_view path, ImportOptions opts);

// Reloads in place every file the import rewrote that was already in memory, so
// importing a source file a second time refreshes what the scene shows instead of
// doing nothing. Handles stay valid. Returns how many assets were refreshed.
size_t reloadImportedAssets(const ImportResult& result, AssetManager& assets);

}  // namespace batap
