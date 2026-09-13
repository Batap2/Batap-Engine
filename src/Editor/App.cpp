#include "App.h"

#include "Engine.h"
#include "FileDialog.h"
#include "Importers/FileImporter.h"
#include "Platform/PlatformWindow.h"
#include "Serialization/EntitySerializer.h"
#include "Components/Camera_C.h"
#include "Components/FreeCamController_C.h"
#include "UI/FieldUI.h"
#include "UI/UIPanels.h"
#include "UI/UITheme.h"
#include "Utils/UIDGenerator.h"

#include <imgui.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

namespace batap
{

App::App(Engine& engine, World& world)
    : ctx_(&engine), world_(&world), assetManager_(ctx_->assetManager_.get())
{
    ui::ApplyTheme();
    installFieldUI();

    EntityHandle camera = world.spawn("camera");
    auto& controller = camera.emplace<FreeCamController_C>();
    controller.controlled_ = true;
    controller.requireRightMouseButton_ = true;

    camera.translate(v3f(0, 2, 6), Space::Local);
    camera.get<Camera_C>().active_ = true;

    loadRecentProjects();
}

// The World outlives the App, so its registry would otherwise be destroyed
// after gameModule_ has unloaded the DLL — and an entt pool created by DLL
// code destroys itself through DLL code.
App::~App()
{
    game_.reset();
    if (world_)
        world_->resetScene();
}

void App::update()
{
    pumpMsgFileDialog();
    pumpGameModuleReload();

    if (state_ == AppState::SelectProject)
    {
        uiPanels_.drawStartupScreen(*this, *ctx_);
    }
    else
    {
        uiPanels_.draw(*world_, *this, *ctx_);
        if (playing_ && game_)
            world_->update(*game_);
        else
            world_->update();
    }
}

void App::pumpGameModuleReload()
{
    if (!gameModule_.stagePending())
        return;

    const std::string snapshot = EntitySerializer::toBuffer(*world_, *ctx_);
    game_.reset();
    world_->resetScene();

    try
    {
        if (gameModule_.swapStaged())
        {
            adoptGame();
            showToast("Game reloaded");
        }
        else
            showToast("Game reload failed (see console)");
    }
    catch (const std::exception& e)
    {
        showToast(std::string("Game reload failed: ") + e.what());
    }

    EntitySerializer::clearSceneAndLoadBuffer(*world_, *ctx_, snapshot);
    uiPanels_.clearSelection();
}

void App::adoptGame()
{
    game_ = gameModule_.makeGame();
    if (gameModule_.api_.gameExeName_)
        gameExeName_ = gameModule_.api_.gameExeName_;
}

void App::showToast(std::string msg)
{
    toast_ = std::move(msg);
    toastEnd_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
}

void App::startPlay()
{
    if (playing_)
        return;
    playSnapshot_ = EntitySerializer::toBuffer(*world_, *ctx_);
    playing_ = true;
    if (game_)
        game_->init(*world_);
}

void App::stopPlay()
{
    if (!playing_)
        return;
    playing_ = false;
    EntitySerializer::clearSceneAndLoadBuffer(*world_, *ctx_, playSnapshot_);
    playSnapshot_.clear();
    // Every EntityHandle from before the reload is dead.
    uiPanels_.clearSelection();
}

static void spawnDetached(const std::string& exe, const std::string& args)
{
#if defined(_WIN32)
    ::ShellExecuteA(nullptr, "open", exe.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
#else
    std::system(("\"" + exe + "\" " + args + " &").c_str());
#endif
}

void App::runStandalone()
{
    if (gameExeName_.empty())
        return;

    namespace fs = std::filesystem;
    const fs::path scene = fs::temp_directory_path() / "batap_run.btpl";
    EntitySerializer::save(*world_, *ctx_, scene.string());

    fs::path exe = fs::path(platformExeDir()) / gameExeName_;
#if defined(_WIN32)
    exe += ".exe";
#endif
    spawnDetached(exe.string(),
                  "--project \"" + projectDir_ + "\" --scene \"" + scene.string() + "\"");
}

// L'emplacement par-utilisateur de chaque OS : %APPDATA% / Application Support
static std::filesystem::path configPath()
{
#if defined(_WIN32)
    char* appdata = nullptr;
    size_t len = 0;
    _dupenv_s(&appdata, &len, "APPDATA");
    std::filesystem::path base = appdata ? appdata : ".";
    free(appdata);
#else
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? std::filesystem::path(home) / "Library/Application Support"
                                      : ".";
#endif
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
    catch (...)
    {}
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
    ctx_->assetManager_->setBaseDir(dir);
    state_ = AppState::Running;

    if (!game_ && !gameModule_.loaded())
    {
        try
        {
            const auto dll = std::filesystem::path(dir) / "bin" / "Game.dll";
            if (std::filesystem::exists(dll) && gameModule_.load(dll.string()))
            {
                adoptGame();
                showToast("Game loaded");
            }
        }
        catch (const std::exception& e)
        {
            showToast(std::string("Game load failed: ") + e.what());
        }
    }

    recentProjects_.erase(std::remove(recentProjects_.begin(), recentProjects_.end(), dir),
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
