#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string_view>

namespace batap
{
struct WindowDesc;

// Per-user configuration files, one per application: the editor's is
// %APPDATA%/BatapEngine/config.json (see editorConfig), a game has its own
// under its own name. The engine reads none of them itself; it gives the
// applications the place, the raw read and the sections it knows how to
// apply, so the game and the editor never share a file. A reader never fails
// on a missing key, and a writer reads the file back first so that what it
// does not know about survives its write.
namespace userConfig
{
// %APPDATA%/<app>/<file> on Windows, ~/Library/Application Support/<app>/<file>
// on macOS.
std::filesystem::path path(std::string_view app, std::string_view file);

// The whole file as an object, empty when absent or unreadable.
nlohmann::json read(const std::filesystem::path& file);

// The "window" section: `screen` is the monitor the window opens centred on
// (platformMonitors() order, 0 the primary), `focusOnShow` false shows it
// without taking the focus. A key the file lacks leaves `io` alone.
void readWindow(const nlohmann::json& config, WindowDesc& io);

// Writes that section and nothing else: the file is read back first, so
// whatever else it holds survives.
void writeWindow(const std::filesystem::path& file, int screen, bool focusOnShow);
}  // namespace userConfig
}  // namespace batap
