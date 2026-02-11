#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include "EigenTypes.h"

namespace batap
{
struct Renderer;
struct InputManager;
struct Scene;
struct SceneRenderer;
struct AssetManager;
struct UIPanels;
struct GPUInstanceManager;
struct Systems;
struct EntityFactory;

template <typename... Msgs>
struct TSMsgBus;

using FileDialogMsg = std::vector<std::string>;
using FileDialogMsgBus = TSMsgBus<FileDialogMsg>;

struct Context
{
    Context();
    ~Context();

    std::unique_ptr<Renderer> _renderer;
    std::unique_ptr<InputManager> _inputManager;
    std::unique_ptr<SceneRenderer> _sceneRenderer;
    std::unique_ptr<Scene> _scene;
    std::unique_ptr<UIPanels> _uiPanels;
    std::unique_ptr<AssetManager> _assetManager;
    std::unique_ptr<GPUInstanceManager> _gpuInstanceManager;
    std::unique_ptr<EntityFactory> _entityFactory;
    std::unique_ptr<FileDialogMsgBus> _fileDialogMsgBus;
    std::unique_ptr<Systems> _systems;

    std::chrono::time_point<std::chrono::high_resolution_clock> _lastTime;
    float _deltaTime = 0;

    void init();
    void update();
    void render();

    v2i getFrameSize();
    uint8_t getFrameindex();
};
}  // namespace batap
