#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Game.h"
#include "GameModuleLoader.h"
#include "EditorIcons.h"
#include "UI/UIPanels.h"
#include "UI/UITheme.h"
#include "FileDialog.h"
#include "World.h"
#include "Components/FreeCamController_C.h"
#include "EigenTypes.h"

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

    void syncEditorCamera();
    v3f editorCamPos_{0.f, 2.f, 6.f};
    quatf editorCamRot_ = quatf::Identity();
    FreeCamController_C editorCamCtrl_;

    Engine* ctx_ = nullptr;
    World*   world_ = nullptr;
    AssetManager* assetManager_ = nullptr;

    UIPanels uiPanels_;
    EditorIcons editorIcons_;

    AppState             state_ = AppState::SelectProject;
    std::string          projectDir_;
    std::vector<std::string> recentProjects_;

    FileDialogMsgBus fileDialogMsgBus_;
    using FileDialogAfterJob = std::function<void(std::vector<std::string>&&)>;
    std::unordered_map<uint64_t, FileDialogAfterJob> fileDialogAfterJobs_;

    void selectProject(const std::string& dir);
    void setTheme(ui::Theme theme);
    ui::Theme theme_ = ui::Theme::Light;
    bool themeDirty_ = false;

    void loadConfig();
    void saveConfig();

    uint64_t openFileDialogAsyncWithAfterJob(std::span<const FileDialogFilter> filters,
                                             FileDialogAfterJob job);
    uint64_t openFolderDialogAsyncWithAfterJob(FileDialogAfterJob job);
    void pumpMsgFileDialog();
};
}  // namespace batap
