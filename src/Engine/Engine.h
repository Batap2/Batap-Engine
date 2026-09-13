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
struct AssetManager;
struct DebugDraw;
struct Billboards;
struct Engine;

struct WindowDesc
{
    std::string title  = "Batap";
    uint32_t    width  = 1280;
    uint32_t    height = 720;
    bool        fpsInTitle = false;
    bool        transparent = false;
};

struct Frame
{
    Frame(const Frame&)            = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&&)                 = delete;
    Frame& operator=(Frame&&)      = delete;
    ~Frame();

    explicit operator bool() const { return alive_; }

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
    uint32_t getFrameindex();

    DebugDraw& debug() { return *debugDraw_; }
    DebugDraw& debugOverlay() { return *debugOverlay_; }
    Billboards& billboards() { return *billboards_; }

    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<InputManager> inputManager_;
    std::unique_ptr<AssetManager> assetManager_;
    std::unique_ptr<DebugDraw> debugDraw_;
    std::unique_ptr<DebugDraw> debugOverlay_;
    std::unique_ptr<Billboards> billboards_;

    float deltaTime_ = 0;

   private:
    friend struct Frame;  // ~Frame calls endFrame()

    void beginFrame();
    void endFrame();
    void updateFpsTitle();

    std::chrono::time_point<std::chrono::high_resolution_clock> lastTime_;

    void* window_ = nullptr;
    std::string title_;
    bool        fpsInTitle_   = false;
    uint32_t    frameCount_   = 0;
    float       fpsElapsed_   = 0.f;
};
}  // namespace batap
