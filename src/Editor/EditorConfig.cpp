#include "EditorConfig.h"

#include "UserConfig.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace batap::editorConfig
{
namespace
{
// The editor's own file; a game keeps its own under its own name
// (UserConfig.h), the two never share one. It was recent.json until
// 2026-10-01: an existing one is taken over, once, so recents, theme and
// cameras follow.
std::filesystem::path configPath()
{
    const auto file = userConfig::path("BatapEngine", "config.json");
    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
    {
        const auto old = userConfig::path("BatapEngine", "recent.json");
        if (std::filesystem::exists(old, ec))
            std::filesystem::rename(old, file, ec);
    }
    return file;
}

nlohmann::json readFile()
{
    return userConfig::read(configPath());
}

// json::value throws on anything but an object, and every lookup below walks
// through keys a hand-edited or older file may simply not have.
nlohmann::json objectAt(const nlohmann::json& j, const std::string& key)
{
    if (!j.is_object())
        return nlohmann::json::object();
    const auto it = j.find(key);
    return it != j.end() && it->is_object() ? *it : nlohmann::json::object();
}

nlohmann::json& objectRef(nlohmann::json& j, const std::string& key)
{
    nlohmann::json& child = j[key];
    if (!child.is_object())
        child = nlohmann::json::object();
    return child;
}
}  // namespace

void readWindow(WindowDesc& io)
{
    userConfig::readWindow(readFile(), io);
}

void writeWindow(int screen, bool focusOnShow)
{
    userConfig::writeWindow(configPath(), screen, focusOnShow);
}

std::string pathKey(const std::string& path)
{
    std::error_code ec;
    const auto abs = std::filesystem::absolute(path, ec);
    return (ec ? std::filesystem::path(path) : abs).lexically_normal().generic_string();
}

std::string sceneKey(const std::string& projectDir, const std::string& scenePath)
{
    if (scenePath.empty() || projectDir.empty())
        return {};

    std::error_code ec;
    const auto rel = std::filesystem::relative(pathKey(scenePath), pathKey(projectDir), ec);
    if (ec || rel.empty() || rel.begin()->string() == "..")
        return pathKey(scenePath);
    return rel.generic_string();
}

void read(File& io, const std::string& projectDir, const std::string& scene)
{
    const nlohmann::json j = readFile();

    io.theme = j.value("theme", std::string("light")) == "dark" ? ui::Theme::Dark
                                                                : ui::Theme::Light;
    io.recent.clear();
    for (const auto& entry : j.value("recent", nlohmann::json::array()))
    {
        auto dir = entry.get<std::string>();
        if (!dir.empty())
            io.recent.push_back(std::move(dir));
    }

    if (projectDir.empty())
        return;

    const nlohmann::json project = objectAt(objectAt(j, "projects"), pathKey(projectDir));

    const nlohmann::json camera = objectAt(project, "camera");
    io.camera.moveSpeed = camera.value("moveSpeed", io.camera.moveSpeed);
    io.camera.boostSpeed = camera.value("boostSpeed", io.camera.boostSpeed);
    io.camera.mouseSensitivity = camera.value("mouseSensitivity", io.camera.mouseSensitivity);
    io.camera.fov = camera.value("fov", io.camera.fov);
    io.camera.znear = camera.value("znear", io.camera.znear);
    io.camera.zfar = camera.value("zfar", io.camera.zfar);

    if (scene.empty())
        return;

    const nlohmann::json pose = objectAt(objectAt(project, "scenes"), scene);
    const auto pos = pose.value("pos", std::vector<float>{});
    if (pos.size() != 3)
        return;

    io.pose.pos = v3f{pos[0], pos[1], pos[2]};
    io.pose.yaw = pose.value("yaw", io.pose.yaw);
    io.pose.pitch = pose.value("pitch", io.pose.pitch);
    io.hasPose = true;
}

void write(const File& file, const std::string& projectDir, const std::string& scene)
{
    const auto path = configPath();
    std::filesystem::create_directories(path.parent_path());

    // Read back first: the file also holds the other projects and the other
    // scenes of this one, which this save must not drop.
    nlohmann::json j = readFile();
    j["recent"] = file.recent;
    j["theme"] = file.theme == ui::Theme::Dark ? "dark" : "light";

    if (!projectDir.empty())
    {
        nlohmann::json& project = objectRef(objectRef(j, "projects"), pathKey(projectDir));

        nlohmann::json& camera = objectRef(project, "camera");
        camera["moveSpeed"] = file.camera.moveSpeed;
        camera["boostSpeed"] = file.camera.boostSpeed;
        camera["mouseSensitivity"] = file.camera.mouseSensitivity;
        camera["fov"] = file.camera.fov;
        camera["znear"] = file.camera.znear;
        camera["zfar"] = file.camera.zfar;

        if (file.hasPose && !scene.empty())
        {
            nlohmann::json& pose = objectRef(objectRef(project, "scenes"), scene);
            pose["pos"] = {file.pose.pos.x(), file.pose.pos.y(), file.pose.pos.z()};
            // The controller drives the rotation from these two, so storing
            // the quaternion instead would snap back on the first mouse move.
            pose["yaw"] = file.pose.yaw;
            pose["pitch"] = file.pose.pitch;
        }
    }

    std::ofstream(path) << j.dump(2);
}

}  // namespace batap::editorConfig
