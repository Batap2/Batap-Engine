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
struct SceneRenderer;
struct AssetManager;
struct Engine;

struct WindowDesc
{
    std::string title  = "Batap";
    uint32_t    width  = 1280;
    uint32_t    height = 720;
    bool        fpsInTitle = false;
};

// The destructor ends the frame (input clear + present). Simulation stays in
// the loop body: skipping world.update() is how you pause.
struct Frame
{
    Frame(const Frame&)            = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&&)                 = delete;
    Frame& operator=(Frame&&)      = delete;
    ~Frame();

    // False once the window asked to close.
    explicit operator bool() const { return alive_; }

    InputManager& input() const;
    float dt() const;

   private:
    friend struct Engine;
    Frame(Engine* engine, bool alive) : engine_(engine), alive_(alive) {}

    Engine* engine_;
    bool    alive_;
};

struct Engine
{
    explicit Engine(const WindowDesc& desc = {});
    ~Engine();

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    Frame nextFrame();

    void setProjectDir(const std::string& dir);

    v2i getFrameSize();
    uint8_t getFrameindex();

    std::unique_ptr<Renderer> _renderer;
    std::unique_ptr<InputManager> _inputManager;
    std::unique_ptr<SceneRenderer> _sceneRenderer;
    std::unique_ptr<AssetManager> _assetManager;

    float _deltaTime = 0;

   private:
    friend struct Frame;  // ~Frame ends the frame

    void beginFrame();
    void endFrame();
    void updateFpsTitle();

    std::chrono::time_point<std::chrono::high_resolution_clock> _lastTime;

    void* window_ = nullptr;
    std::string title_;
    bool        fpsInTitle_   = false;
    uint32_t    frameCount_   = 0;
    float       fpsElapsed_   = 0.f;
};
}  // namespace batap
