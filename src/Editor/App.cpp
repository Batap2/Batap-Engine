#include "App.h"

#include "Context.h"
#include "Importers/FileImporter.h"
#include "Renderer/SceneRenderer.h"
#include "TestScene.h"
#include "UI/UITheme.h"
#include "UI/UIPanels.h"
#include "Utils/UIDGenerator.h"
#include "WindowsUtils/FileDialog.h"

#include <imgui.h>
#include <memory>

namespace batap
{

void App::start(Context& ctx)
{
    ctx_          = &ctx;
    assetManager_ = ctx._assetManager.get();

    ui::ApplyTheme();

    world_        = std::make_unique<World>(ctx);
    world_->scene_ = std::make_unique<TestScene>(*world_);
}

void App::update(Context& ctx)
{
    ctx.beginFrame();

    pumpMsgFileDialog();

    uiPanels_.draw(*world_, *this, ctx);

    world_->update(ctx);
    ctx._sceneRenderer->setScene(
        {&world_->scene_.get()->_registry, world_->instanceManager_.get()});
    ctx._sceneRenderer->uploadDirty();
    ctx.endFrame();
}

void App::shutdown(Context& /*ctx*/) {}

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
