#pragma once

#include <chrono>
#include <memory>
#include "Renderer/SceneRenderer.h"

namespace rayvox
{
struct Renderer;
struct InputManager;
struct Scene;
struct AssetManager;

struct Context
{
    Context();

    Renderer* _renderer;
    InputManager* _inputManager;
    SceneRenderer* _sceneRenderer;
    Scene* _scene;
    //std::unique_ptr<AssetManager> _assetManager;

    std::chrono::time_point<std::chrono::high_resolution_clock> _lastTime;
    float _deltaTime = 0;

    void init();
    void update();
    void render();
};
}  // namespace rayvox