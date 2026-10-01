#include "UserConfig.h"

#include "Engine.h"  // WindowDesc

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace batap::userConfig
{

std::filesystem::path path(std::string_view app, std::string_view file)
{
#if defined(_WIN32)
    char* appdata = nullptr;
    size_t len = 0;
    _dupenv_s(&appdata, &len, "APPDATA");
    std::filesystem::path base = appdata ? appdata : ".";
    std::free(appdata);
#else
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? std::filesystem::path(home) / "Library/Application Support"
                                      : ".";
#endif
    return base / app / file;
}

nlohmann::json read(const std::filesystem::path& file)
{
    std::ifstream f(file);
    if (!f.is_open())
        return nlohmann::json::object();
    try
    {
        auto j = nlohmann::json::parse(f);
        if (j.is_object())
            return j;
    }
    catch (...)
    {}
    return nlohmann::json::object();
}

void readWindow(const nlohmann::json& config, WindowDesc& io)
{
    if (!config.is_object())
        return;
    const auto it = config.find("window");
    if (it == config.end() || !it->is_object())
        return;

    io.screen = it->value("screen", io.screen);
    io.focusOnShow = it->value("focusOnShow", io.focusOnShow);
}

void writeWindow(const std::filesystem::path& file, int screen, bool focusOnShow)
{
    nlohmann::json j = read(file);
    j["window"] = {{"screen", screen}, {"focusOnShow", focusOnShow}};
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream(file) << j.dump(2);
}

}  // namespace batap::userConfig
