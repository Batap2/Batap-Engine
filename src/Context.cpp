#include "Context.h"

#include <chrono>
#include <cstddef>

#include "InputManager.h"
#include "Renderer/Renderer.h"
#include "Scene.h"


void printDeltaTime(float dt)
{
    static int counter = 0;
    static float deltaTimeBuffer[100];
    static size_t dt_index = 0;
    static std::chrono::time_point<std::chrono::high_resolution_clock> lastPrint = std::chrono::high_resolution_clock::now();

    deltaTimeBuffer[dt_index] = dt;
    dt_index = (dt_index + 1) % 100;

    counter++;

    std::chrono::duration<float> dtp = std::chrono::high_resolution_clock::now() - lastPrint;
    if(dtp.count() > 1){
        float dtAvg = 0;
        for(int i = 0; i < 100; i++){
            dtAvg += deltaTimeBuffer[i];
        }
        dtAvg /= 100;

        std::cout << counter << "\n";
        counter = 0;
        //std::cout << dtAvg*1000 << "ms\n";
        lastPrint = std::chrono::high_resolution_clock::now();
    }
}

namespace rayvox
{
Context::Context()
{
    _renderer = new Renderer();
    _inputManager = new InputManager();
    _inputManager->Ctx = this;
    _lastTime = std::chrono::high_resolution_clock::now();
}

void Context::update()
{
    std::chrono::duration<float> dt = std::chrono::high_resolution_clock::now() - _lastTime;
    _lastTime = std::chrono::high_resolution_clock::now();
    _deltaTime = dt.count();
    printDeltaTime(_deltaTime);

    _inputManager->DispatchEvents();
    _inputManager->ClearFrameState();

    //_scene->update(1);

    _renderer->render();
}

void Context::render()
{
}
}  // namespace rayvox