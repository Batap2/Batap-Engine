#include "Engine.h"

#include "Renderer/Billboards.h"
#include "Renderer/DebugDraw.h"

#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "InputManager.h"
#include "Platform/PlatformWindow.h"
#include "Reflection/ComponentRegistry.h"
#include "Renderer/Renderer.h"
#include "Serialization/AssetFieldTypes.h"
#include "Serialization/PhysicsFieldTypes.h"

#include <filesystem>
#include <stdexcept>

namespace batap
{
Frame::~Frame()
{
    if (!alive_)
        return;

    engine_->endFrame();
}

Engine::Engine(const WindowDesc& desc) : title_(desc.title), fpsInTitle_(desc.fpsInTitle)
{
    // Components self-registered at static init (BATAP_COMPONENT); field
    // serializers arrive now. Validate the whole registry while a stack
    // trace still points here rather than at the first load/save.
    registerBuiltinFieldTypes();
    registerAssetFieldTypes();
    registerPhysicsFieldTypes();
    ComponentRegistry::instance().validate();

    platformInit();

    window_ = platformCreateWindow(desc);
    if (!window_)
        throw std::runtime_error("Engine: failed to create the window");

    inputManager_ = std::make_unique<InputManager>();
    lastTime_ = std::chrono::steady_clock::now();

    renderer_ = std::make_unique<Renderer>(window_, desc.transparent);
    assetManager_ = std::make_unique<AssetManager>(renderer_->resourceManager_);
    debugDraw_ = std::make_unique<DebugDraw>();
    debugOverlay_ = std::make_unique<DebugDraw>();
    billboards_ = std::make_unique<Billboards>();
    createDefaultAssets(*this);

    // `--project <dir>` is how dev launch configs point a build-tree exe at
    // its assets; without it, assets are expected next to the executable
    // (shipped layout). setProjectDir() can still override later.
    std::string projectDir = platformExeDir();
    const auto args = platformCommandLineArgs();
    for (size_t i = 0; i + 1 < args.size(); ++i)
    {
        if (args[i] == "--project")
        {
            projectDir = args[i + 1];
            break;
        }
    }
    setProjectDir(projectDir);

    // The message procedure may talk to ImGui and the input manager as soon
    // as an Engine is bound — so only now.
    platformBindContext(window_, this);
    platformShowWindow(window_, desc.focusOnShow);
}

Engine::~Engine()
{
    if (window_)
        platformBindContext(window_, nullptr);
    if (renderer_)
        renderer_->flush();
}

Frame Engine::nextFrame()
{
    limitFrameRate();
    if (!platformPumpMessages())
        return Frame{this, false};

    beginFrame();

    if (fpsInTitle_)
        updateFpsTitle();

    return Frame{this, true};
}

void Engine::beginFrame()
{
    const auto now = std::chrono::steady_clock::now();
    deltaTime_ = std::chrono::duration<float>(now - lastTime_).count();
    lastTime_ = now;

    renderer_->beginFrame();
    inputManager_->DispatchEvents();

    ++frameNumber_;
    if (viewRectFrame_ + 1 < frameNumber_)
        applyViewRect({v2i::Zero(), v2i(static_cast<int>(renderer_->width_),
                                        static_cast<int>(renderer_->height_))});
}

ViewRect Engine::viewRect() const
{
    const VkExtent2D view = renderer_->viewExtent();
    return {viewOrigin_, v2i(static_cast<int>(view.width), static_cast<int>(view.height))};
}

void Engine::setViewRect(const ViewRect& rect)
{
    viewRectFrame_ = frameNumber_;
    applyViewRect(rect);
}

void Engine::applyViewRect(const ViewRect& rect)
{
    const v2i window(static_cast<int>(renderer_->width_), static_cast<int>(renderer_->height_));
    viewOrigin_ = rect.origin_.cwiseMax(0).cwiseMin(window);
    const v2i size = rect.size_.cwiseMin(window - viewOrigin_).cwiseMax(1);
    renderer_->setViewExtent(static_cast<uint32_t>(size.x()), static_cast<uint32_t>(size.y()));
}

uint64_t Engine::sceneTexture()
{
    return static_cast<uint64_t>(renderer_->sceneTexture());
}

void Engine::endFrame()
{
    inputManager_->ClearFrameState();
    renderer_->uploadDebugDraw(*debugDraw_, *debugOverlay_);
    renderer_->uploadBillboards(*billboards_);
    debugDraw_->endFrame(deltaTime_);
    debugOverlay_->endFrame(deltaTime_);
    billboards_->endFrame(deltaTime_);
    renderer_->render();
}

void Engine::limitFrameRate()
{
    if (settings_.maxFps_ == 0)
        return;
    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / settings_.maxFps_));
    platformSleepUntil(lastTime_ + period);
}

void Engine::setVsync(bool on)
{
    settings_.vsync_ = on;
    renderer_->setVsync(on);
}

void Engine::updateFpsTitle()
{
    ++frameCount_;
    fpsElapsed_ += deltaTime_;

    if (fpsElapsed_ < 1.f)
        return;

    platformSetWindowTitle(window_, title_ + " - " + std::to_string(frameCount_) + " fps");
    frameCount_ = 0;
    fpsElapsed_ = 0.f;
}

void Engine::setProjectDir(const std::string& dir)
{
    assetManager_->setBaseDir(std::filesystem::absolute(dir).string());
}

v2i Engine::getFrameSize()
{
    const VkExtent2D view = renderer_->viewExtent();
    return {static_cast<int>(view.width), static_cast<int>(view.height)};
}

uint32_t Engine::getFrameindex()
{
    return renderer_->frameIndex();
}
}  // namespace batap
