#pragma once

#include <chrono>
#include <memory>
#include <string>

namespace rayvox
{
struct Renderer;
struct InputManager;
struct Scene;
struct SceneRenderer;
struct AssetManager;
struct UIPanels;
struct GPUInstanceManager;

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
    std::unique_ptr<FileDialogMsgBus> _fileDialogMsgBus;

    std::chrono::time_point<std::chrono::high_resolution_clock> _lastTime;
    float _deltaTime = 0;

    void init();
    void update();
    void render();
};
}  // namespace rayvox
