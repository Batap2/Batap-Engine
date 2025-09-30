#pragma once

namespace RayVox {
    struct Renderer;
    struct InputManager;
    struct Scene;

    struct Context{
        
        Context();

        Renderer* renderer;
        InputManager* inputManager;
        Scene* scene;
    
        void Update();
        void render();
    };
}