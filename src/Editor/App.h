#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "UI/UIPanels.h"
#include "WindowsUtils/FileDialog.h"
#include "World.h"

namespace batap
{
struct Engine;
struct AssetManager;

enum class AppState { SelectProject, Running };

struct App
{
    App(Engine& engine, World& world);

    void update();

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
