#include "Context.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>

#include "Assets/AssetManager.h"
#include "EigenTypes.h"
#include "Importers/FileImporter.h"
#include "InputManager.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/SceneRenderer.h"
#include "Scene.h"
#include "UI/UIPanels.h"
#include "TestScene.h"
#include "WindowsUtils/FileDialog.h"


static void printDeltaTime(float dt)
{
    static int counter = 0;
    static std::array<float, 100> deltaTimeBuffer;
    static size_t dt_index = 0;
    static std::chrono::time_point<std::chrono::high_resolution_clock> lastPrint =
        std::chrono::high_resolution_clock::now();

    deltaTimeBuffer[dt_index] = dt;
    dt_index = (dt_index + 1) % 100;

    counter++;

    std::chrono::duration<float> dtp = std::chrono::high_resolution_clock::now() - lastPrint;
    if (dtp.count() > 1)
    {
        // float dtAvg = 0;
        // for (int i = 0; i < 100; i++)
        // {
        //     dtAvg += deltaTimeBuffer[i];
        // }
        // dtAvg /= 100;

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
    _renderer = std::make_unique<Renderer>();  // inited just before Context::init()
    _inputManager = std::make_unique<InputManager>();
    _inputManager->Ctx = this;
    _lastTime = std::chrono::high_resolution_clock::now();
    _uiPanels = std::make_unique<UIPanels>(*this);
    _fileDialogMsgBus = std::make_unique<FileDialogMsgBus>();
}

Context::~Context() = default;

void Context::init()
{
    _gpuInstanceManager = std::make_unique<GPUInstanceManager>(*_renderer->_resourceManager);
    _assetManager = std::make_unique<AssetManager>(_renderer->_resourceManager);
    _scene = std::make_unique<TestScene>(*_gpuInstanceManager.get());
    _sceneRenderer = std::make_unique<SceneRenderer>(*this);
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
            for (auto& path : msg)
            {
                importFile(path, *_assetManager.get());
            }
        });

    _renderer->beginImGuiFrame();

    _inputManager->DispatchEvents();
    _inputManager->ClearFrameState();

    _scene->update(1);
    _sceneRenderer->uploadDirty(_renderer->_frameIndex);

    _uiPanels->Draw();

    _renderer->render();
}

void Context::render() {}
}  // namespace rayvox
