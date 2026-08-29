#include "Engine.h"

#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "InputManager.h"
#include "Platform/PlatformWindow.h"
#include "Reflection/ComponentRegistry.h"
#include "Renderer/Renderer.h"
#include "Serialization/AssetFieldTypes.h"


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

InputManager& Frame::input() const
{
    return *engine_->inputManager_;
}

float Frame::dt() const
{
    return engine_->deltaTime_;
}

Engine::Engine(const WindowDesc& desc) : title_(desc.title), fpsInTitle_(desc.fpsInTitle)
{
    // Components self-registered at static init (BATAP_COMPONENT); field
    // serializers arrive now. Validate the whole registry while a stack
    // trace still points here rather than at the first load/save.
    registerBuiltinFieldTypes();
    registerAssetFieldTypes();
    ComponentRegistry::instance().validate();

    platformInit();

    window_ = platformCreateWindow(desc);
    if (!window_)
        throw std::runtime_error("Engine: failed to create the window");

    inputManager_ = std::make_unique<InputManager>();
    lastTime_ = std::chrono::high_resolution_clock::now();

    renderer_ = std::make_unique<Renderer>(window_, desc.transparent);
    assetManager_ = std::make_unique<AssetManager>(renderer_->resourceManager_);
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
    platformShowWindow(window_);
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
    if (!platformPumpMessages())
        return Frame{this, false};

    beginFrame();

    if (fpsInTitle_)
        updateFpsTitle();

    return Frame{this, true};
}

void Engine::beginFrame()
{
    std::chrono::duration<float> dt = std::chrono::high_resolution_clock::now() - lastTime_;
    lastTime_ = std::chrono::high_resolution_clock::now();
    deltaTime_ = dt.count();

    renderer_->beginFrame();
    inputManager_->DispatchEvents();
}

void Engine::endFrame()
{
    inputManager_->ClearFrameState();
    renderer_->render();
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
    return {renderer_->width_, renderer_->height_};
}

uint32_t Engine::getFrameindex()
{
    return renderer_->frameIndex();
}
}  // namespace batap
