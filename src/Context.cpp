#include "Context.h"
#include "InputManager.h"
#include "Renderer/Renderer.h"

#include <chrono>

void printFps(){
    static uint64_t frameCounter = 0;
    static double elapsedSeconds = 0.0;
    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();

    frameCounter++;
    auto t1 = clock.now();
    auto deltaTime = t1 - t0;
    t0 = t1;
    elapsedSeconds += deltaTime.count() * 1e-9;
    if (elapsedSeconds > 1.0)
    {
        char buffer[500];
        auto fps = frameCounter / elapsedSeconds;
        sprintf_s(buffer, 500, "FPS: %f\n", fps);
        std::cout << buffer;

        frameCounter = 0;
        elapsedSeconds = 0.0;

    }
}

namespace RayVox{
    Context::Context(){
        renderer = new Renderer();
        inputManager = new InputManager();
        inputManager->Ctx = this;
    }

    

    void Context::Update(){
        printFps();
        
        inputManager->DispatchEvents();
        inputManager->ClearFrameState();

        //renderer->render();
    }
    
    void Context::render(){
    }
}