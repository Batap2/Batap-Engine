#include "Engine.h"

#include "Assets/AssetLoader.h"
#include "Assets/AssetManager.h"
#include "InputManager.h"
#include "Platform/PlatformWindow.h"
#include "Reflection/ComponentRegistry.h"
#include "Renderer/Renderer.h"
#include "Renderer/SceneRenderer.h"

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
    return *engine_->_inputManager;
}

float Frame::dt() const
{
    return engine_->_deltaTime;
}

Engine::Engine(const WindowDesc& desc) : title_(desc.title), fpsInTitle_(desc.fpsInTitle)
{
    // Components self-registered at static init (BATAP_COMPONENT); field
    // serializers arrive now. Validate the whole registry while a stack
    // trace still points here rather than at the first load/save.
    registerBuiltinFieldTypes();
    ComponentRegistry::instance().validate();

    platformInit();

    window_ = platformCreateWindow(desc);
    if (!window_)
        throw std::runtime_error("Engine: failed to create the window");

    _renderer = std::make_unique<Renderer>();
    _inputManager = std::make_unique<InputManager>();
    _lastTime = std::chrono::high_resolution_clock::now();

    // Order matters: Renderer::init creates the ResourceManager the
    // AssetManager needs.
    _renderer->init(static_cast<HWND>(window_), desc.width, desc.height);
    _assetManager = std::make_unique<AssetManager>(_renderer->_resourceManager);
    createDefaultAssets(*this);
    _sceneRenderer = std::make_unique<SceneRenderer>(*this);
    _sceneRenderer->initRenderPasses();

    // `--project <dir>` is how dev launch configs point a build-tree exe at
    // its assets; without it, assets are expected next to the executable
    // (shipped layout). setProjectDir() can still override later.
    std::string projectDir = platformExeDir();
    const auto  args       = platformCommandLineArgs();
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
    if (_renderer)
        _renderer->flush();
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
    std::chrono::duration<float> dt = std::chrono::high_resolution_clock::now() - _lastTime;
    _lastTime = std::chrono::high_resolution_clock::now();
    _deltaTime = dt.count();

    _renderer->beginImGuiFrame();
    _inputManager->DispatchEvents();
}

void Engine::endFrame()
{
    _inputManager->ClearFrameState();
    _renderer->render();
}

void Engine::updateFpsTitle()
{
    ++frameCount_;
    fpsElapsed_ += _deltaTime;

    if (fpsElapsed_ < 1.f)
        return;

    platformSetWindowTitle(window_, title_ + " - " + std::to_string(frameCount_) + " fps");
    frameCount_ = 0;
    fpsElapsed_ = 0.f;
}

void Engine::setProjectDir(const std::string& dir)
{
    _assetManager->setBaseDir(std::filesystem::absolute(dir).string());
}

v2i Engine::getFrameSize()
{
    return {_renderer->_width, _renderer->_height};
}

uint8_t Engine::getFrameindex()
{
    return _renderer->_frameIndex;
}
}  // namespace batap
