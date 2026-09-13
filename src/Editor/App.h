#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Game.h"
#include "GameModuleLoader.h"
#include "UI/UIPanels.h"
#include "FileDialog.h"
#include "World.h"

namespace batap
{
struct Engine;
struct AssetManager;

enum class AppState { SelectProject, Running };

struct App
{
    App(Engine& engine, World& world);
    ~App();

    void update();

    // Play serializes the scene to memory and
    // starts ticking the game; Stop reloads the snapshot as if it were a file.
    void startPlay();
    void stopPlay();
    bool playing_ = false;
    std::string playSnapshot_;
    // Declared before game_: a DLL game must be destroyed before its module.
    GameModuleLoader gameModule_;
    std::unique_ptr<Game> game_;

    // Current scene → temp file → game exe in its own process.
    void runStandalone();
    std::string gameExeName_;

    void pumpGameModuleReload();
    void adoptGame();

    void showToast(std::string msg);
    std::string toast_;
    std::chrono::steady_clock::time_point toastEnd_{};

    Engine* ctx_ = nullptr;
    World*   world_ = nullptr;
    AssetManager* assetManager_ = nullptr;

    UIPanels uiPanels_;

    AppState             state_ = AppState::SelectProject;
    std::string          projectDir_;
    std::vector<std::string> recentProjects_;

    FileDialogMsgBus fileDialogMsgBus_;
    using FileDialogAfterJob = std::function<void(std::vector<std::string>&&)>;
    std::unordered_map<uint64_t, FileDialogAfterJob> fileDialogAfterJobs_;

    void selectProject(const std::string& dir);
    void loadRecentProjects();
    void saveRecentProjects();

    uint64_t openFileDialogAsyncWithAfterJob(std::span<const FileDialogFilter> filters,
                                             FileDialogAfterJob job);
    uint64_t openFolderDialogAsyncWithAfterJob(FileDialogAfterJob job);
    void pumpMsgFileDialog();
};
}  // namespace batap
