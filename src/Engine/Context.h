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
    std::unique_ptr<AssetManager> _assetManager;
    std::unique_ptr<FileDialogMsgBus> _fileDialogMsgBus;

    std::chrono::time_point<std::chrono::high_resolution_clock> _lastTime;
    float _deltaTime = 0;

    void init();
    void beginFrame();
    void endFrame();
    void render();
    void destroy();

    v2i getFrameSize();
    uint8_t getFrameindex();
};
}  // namespace batap
