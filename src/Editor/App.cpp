#include "App.h"

#include "Engine.h"
#include "Importers/FileImporter.h"
#include "TestScene.h"
#include "UI/UITheme.h"
#include "UI/UIPanels.h"
#include "Utils/UIDGenerator.h"
#include "WindowsUtils/FileDialog.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>

namespace batap
{

App::App(Engine& engine, World& world)
    : ctx_(&engine), world_(&world), assetManager_(ctx_->_assetManager.get())
{
    ui::ApplyTheme();

    world.scene_ = std::make_unique<TestScene>(world);

    loadRecentProjects();
}

void App::update()
{
    pumpMsgFileDialog();

    if (state_ == AppState::SelectProject)
    {
        uiPanels_.drawStartupScreen(*this, *ctx_);
    }
    else
    {
        uiPanels_.draw(*world_, *this, *ctx_);
        world_->update();
    }
}

static std::filesystem::path configPath()
{
    char* appdata = nullptr;
    size_t len    = 0;
    _dupenv_s(&appdata, &len, "APPDATA");
    std::filesystem::path base = appdata ? appdata : ".";
    free(appdata);
    return base / "BatapEngine" / "recent.json";
}

void App::loadRecentProjects()
{
    auto path = configPath();
    if (!std::filesystem::exists(path))
        return;
    std::ifstream f(path);
    if (!f.is_open())
        return;
    try
    {
        auto j = nlohmann::json::parse(f);
        for (auto& s : j.value("recent", nlohmann::json::array()))
        {
            auto str = s.get<std::string>();
            if (!str.empty())
                recentProjects_.push_back(std::move(str));
        }
    }
    catch (...) {}
}

void App::saveRecentProjects()
{
    auto path = configPath();
    std::filesystem::create_directories(path.parent_path());
    nlohmann::json j;
    j["recent"] = recentProjects_;
    std::ofstream(path) << j.dump(2);
}

void App::selectProject(const std::string& dir)
{
    projectDir_ = dir;
    ctx_->_assetManager->setBaseDir(dir);
    state_ = AppState::Running;

    recentProjects_.erase(
        std::remove(recentProjects_.begin(), recentProjects_.end(), dir),
        recentProjects_.end());
    recentProjects_.insert(recentProjects_.begin(), dir);
    if (recentProjects_.size() > 10)
        recentProjects_.resize(10);
    saveRecentProjects();
}

uint64_t App::openFileDialogAsyncWithAfterJob(std::span<const FileDialogFilter> filters,
                                              FileDialogAfterJob job)
{
    auto id = next_uid64();
    fileDialogAfterJobs_.emplace(id, std::move(job));
    OpenFilesDialogAsync(filters, &fileDialogMsgBus_, id);
    return id;
}

uint64_t App::openFolderDialogAsyncWithAfterJob(FileDialogAfterJob job)
{
    auto id = next_uid64();
    fileDialogAfterJobs_.emplace(id, std::move(job));
    OpenFolderDialogAsync(&fileDialogMsgBus_, id);
    return id;
}

void App::pumpMsgFileDialog()
{
    fileDialogMsgBus_.pumpType<FileDialogMsg>(
        [&](FileDialogMsg&& msg)
        {
            auto it = fileDialogAfterJobs_.find(msg.id_);
            if (it != fileDialogAfterJobs_.end())
            {
                it->second(std::move(msg.paths_));
                fileDialogAfterJobs_.erase(it);
                return;
            }

            for (auto& path : msg.paths_)
                importFile(path, ImportOptions{projectDir_});
        });
}

}  // namespace batap
