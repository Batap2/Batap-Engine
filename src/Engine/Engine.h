#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "EigenTypes.h"
#include "ViewRect.h"

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
    std::string title = "Batap";
    // Logical units, not pixels: the framebuffer is larger on a HiDPI screen.
    uint32_t width = 1280;
    uint32_t height = 720;
    bool fpsInTitle = false;
    bool transparent = false;
};

struct EngineSettings
{
    uint32_t maxFps_ = 0;
    bool vsync_ = false;
};

struct Frame
{
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&&) = delete;
    Frame& operator=(Frame&&) = delete;
    ~Frame();

    explicit operator bool() const { return alive_; }

   private:
    friend struct Engine;
    Frame(Engine* engine, bool alive) : engine_(engine), alive_(alive) {}

    Engine* engine_;
    bool alive_;
};

struct Engine
{
    explicit Engine(const WindowDesc& desc = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    Frame nextFrame();

    void setProjectDir(const std::string& dir);

    v2i getFrameSize();
    uint32_t getFrameindex();

    DebugDraw& debug() { return *debugDraw_; }
    DebugDraw& debugOverlay() { return *debugOverlay_; }
    Billboards& billboards() { return *billboards_; }

    // Where the scene is rendered, in pixels: the whole window unless a host
    // sets it every frame (the editor, between its panels). getFrameSize() is
    // its size.
    ViewRect viewRect() const;
    void setViewRect(const ViewRect& rect);

    // The scene as an ImGui texture. Asking for it is what makes the renderer
    // draw into an image instead of the window.
    uint64_t sceneTexture();

    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<InputManager> inputManager_;
    std::unique_ptr<AssetManager> assetManager_;
    std::unique_ptr<DebugDraw> debugDraw_;
    std::unique_ptr<DebugDraw> debugOverlay_;
    std::unique_ptr<Billboards> billboards_;

    const EngineSettings& settings() const { return settings_; }
    // 0 = uncapped. CPU sleep
    void setMaxFps(uint32_t fps) { settings_.maxFps_ = fps; }
    void setVsync(bool on);

    float deltaTime_ = 0;

    // ImGui points. Read by the platform layer to know what drags the window.
    float titleBarHeight_ = 0.0f;

    void* nativeWindow() const { return window_; }

   private:
    friend struct Frame;  // ~Frame calls endFrame()

    void limitFrameRate();
    void beginFrame();
    void endFrame();
    void updateFpsTitle();

    EngineSettings settings_;
    std::chrono::steady_clock::time_point lastTime_;

    void* window_ = nullptr;
    std::string title_;
    bool fpsInTitle_ = false;
    uint32_t frameCount_ = 0;
    float fpsElapsed_ = 0.f;

    void applyViewRect(const ViewRect& rect);
    v2i viewOrigin_ = v2i::Zero();
    uint64_t frameNumber_ = 0;
    uint64_t viewRectFrame_ = 0;
};
}  // namespace batap
