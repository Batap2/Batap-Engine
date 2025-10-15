#pragma once

#include <chrono>

namespace rayvox
{
struct Renderer;
struct InputManager;
struct Scene;

struct Context
{
    Context();

    Renderer* _renderer;
    InputManager* _inputManager;
    Scene* _scene;

    std::chrono::time_point<std::chrono::high_resolution_clock> _lastTime;
    float _deltaTime = 0;

    void update();
    void render();
};
}  // namespace rayvox