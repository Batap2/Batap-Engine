#include "Context.h"

#include <chrono>
#include <cstddef>
#include <memory>

#include "Assets/AssetManager.h"
#include "InputManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/SceneRenderer.h"
#include "Scene.h"
#include "UI/UIPanels.h"
#include "VoxelRayScene.h"
#include "WindowsUtils/FileDialog.h"


#include "EigenTypes.h"

void printDeltaTime(float dt)
{
    static int counter = 0;
    static float deltaTimeBuffer[100];
    static size_t dt_index = 0;
    static std::chrono::time_point<std::chrono::high_resolution_clock> lastPrint =
        std::chrono::high_resolution_clock::now();

    deltaTimeBuffer[dt_index] = dt;
    dt_index = (dt_index + 1) % 100;

    counter++;

    std::chrono::duration<float> dtp = std::chrono::high_resolution_clock::now() - lastPrint;
    if (dtp.count() > 1)
    {
        float dtAvg = 0;
        for (int i = 0; i < 100; i++)
        {
            dtAvg += deltaTimeBuffer[i];
        }
        dtAvg /= 100;

        std::cout << counter << "\n";
        counter = 0;
        // std::cout << dtAvg*1000 << "ms\n";
        lastPrint = std::chrono::high_resolution_clock::now();
    }
}

namespace rayvox
{
Context::Context()
{
    _renderer = std::make_unique<Renderer>();
    _inputManager = std::make_unique<InputManager>();
    _inputManager->Ctx = this;
    _lastTime = std::chrono::high_resolution_clock::now();
    _uiPanels = std::make_unique<UIPanels>(*this);
    _fileDialogMsgBus = std::make_unique<FileDialogMsgBus>();
}

Context::~Context() = default;

void Context::init()
{
    _scene = std::make_unique<VoxelRayScene>();
    _sceneRenderer = std::make_unique<SceneRenderer>(_renderer.get());
    _sceneRenderer->loadScene(_scene.get());
    _sceneRenderer->initRenderPasses();
}

void Context::update()
{
    std::chrono::duration<float> dt = std::chrono::high_resolution_clock::now() - _lastTime;
    _lastTime = std::chrono::high_resolution_clock::now();
    _deltaTime = dt.count();
    printDeltaTime(_deltaTime);

    _fileDialogMsgBus->pumpType<FileDialogMsg>(
        [&](FileDialogMsg&& msg)
        {
            for(auto& path : msg){
                std::cout << path << "\n";
            }
        });

    _renderer->beginImGuiFrame();

    _inputManager->DispatchEvents();
    _inputManager->ClearFrameState();

    _scene->update(1);
    _sceneRenderer->uploadDirty();

    _uiPanels->Draw();

    _renderer->render();
}

void Context::render() {}
}  // namespace rayvox