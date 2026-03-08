#include "App.h"
#include <memory>

#include "Assets/AssetHandle.h"
#include "Context.h"
#include "Importers/FileImporter.h"
#include "Renderer/SceneRenderer.h"
#include "TestScene.h"
#include "UI/UIPanels.h"
#include "Utils/UIDGenerator.h"
#include "WindowsUtils/FileDialog.h"


#include <imgui.h>

namespace batap
{

void App::start(Context& ctx)
{
    world_ = std::make_unique<World>(ctx);
    world_->scene_ = std::make_unique<TestScene>(*world_);
    assetManager_ = ctx._assetManager.get();
}

void App::update(Context& ctx)
{
    ctx.beginFrame();

    pumpMsgFileDialog(ctx);

    uiPanels_.draw(*world_, *this);

    world_->update(ctx);
    ctx._sceneRenderer->setScene(
        {&world_->scene_.get()->_registry, world_->instanceManager_.get()});
    ctx._sceneRenderer->uploadDirty();
    ctx.endFrame();
}

void App::shutdown(Context& ctx) {}

uint64_t App::openFileDialogAsyncWithAfterJob(std::span<const FileDialogFilter> filters,
                                              FileDialogAfterJob job)
{
    auto id = next_uid64();
    fileDialogAfterJobs_.emplace(id, std::move(job));
    OpenFilesDialogAsync(filters, &fileDialogMsgBus_, id);
    return id;
}

void App::pumpMsgFileDialog(Context& ctx)
{
    fileDialogMsgBus_.pumpType<FileDialogMsg>(
        [&](FileDialogMsg&& msg)
        {
            MeshHandle lastImported; // temporary solution
            for (auto& path : msg.paths_)
            {
                auto handles = importFile(path, *ctx._assetManager.get());
            }
        });
}

}  // namespace batap
