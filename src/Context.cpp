#include "Context.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>

#include "Assets/AssetManager.h"
#include "EigenTypes.h"
#include "Importers/FileImporter.h"
#include "InputManager.h"
#include "Instance/EntityFactory.h"
#include "Instance/InstanceManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/SceneRenderer.h"
#include "Scene.h"
#include "Systems/Systems.h"
#include "TestScene.h"
#include "UI/UIPanels.h"
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
    _systems = std::make_unique<Systems>(*this);
}

Context::~Context() = default;

void Context::init()
{
    _gpuInstanceManager = std::make_unique<GPUInstanceManager>(*this);
    _entityFactory = std::make_unique<EntityFactory>(*_gpuInstanceManager.get());
    _assetManager = std::make_unique<AssetManager>(_renderer->_resourceManager);
    _scene = std::make_unique<TestScene>(*this);
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

    _scene->update(_deltaTime);
    _systems->update(_deltaTime, _scene->_registry);
    _sceneRenderer->uploadDirty(_renderer->_frameIndex);

    _inputManager->ClearFrameState();

    _uiPanels->Draw();

    _renderer->render();
}

void Context::render() {}

v2i Context::getFrameSize()
{
    return {_renderer->_width, _renderer->_height};
}

uint8_t Context::getFrameindex()
{
    return _renderer->_frameIndex;
}
}  // namespace rayvox
