#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include "IApp.h"
#include "UI/UIPanels.h"
#include "WindowsUtils/FileDialog.h"
#include "World.h"

namespace batap
{
struct Context;
struct AssetManager;

struct App : IApp
{
    Context*               ctx_ = nullptr;
    std::unique_ptr<World> world_;
    AssetManager*          assetManager_;

    UIPanels uiPanels_;

    std::string projectDir_;

    FileDialogMsgBus fileDialogMsgBus_;
    using FileDialogAfterJob = std::function<void(std::vector<std::string>&&)>;
    std::unordered_map<uint64_t, FileDialogAfterJob> fileDialogAfterJobs_;

    void start(Context& ctx) override;
    void update(Context& ctx) override;
    void shutdown(Context& ctx) override;

    uint64_t openFileDialogAsyncWithAfterJob(std::span<const FileDialogFilter> filters,
                                             FileDialogAfterJob job);
    uint64_t openFolderDialogAsyncWithAfterJob(FileDialogAfterJob job);
    void pumpMsgFileDialog();
};
}  // namespace batap
