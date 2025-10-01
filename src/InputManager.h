#pragma once

#include <wrl.h>
using namespace Microsoft::WRL;
#include "nano_signal_slot.hpp"
#include <glm/glm.hpp>
#include <unordered_set>


using namespace glm;
namespace RayVox{
    struct Context;
    
    struct InputManager
    {
        Context* Ctx;
    
        enum class MouseButton { Left = 0, Right = 1, Middle = 2 };
        enum class KeyState{Pressed, Released};

        std::unordered_set<unsigned long long> KeysDown;
        std::unordered_set<unsigned long long> KeysPressed;
        std::unordered_set<unsigned long long> KeysReleased;
        bool MouseButtonsDown[3] = {false};
        bool MouseButtonsPressed[3] = {false};
        bool MouseButtonsReleased[3] = {false};
        ivec2 MouseDeltaAccumulated = {0, 0};
        ivec2 MousePosition = {0, 0};
        float MouseWheelAccumulated = 0.0f;
    
        struct KeyEvent{
            KeyState Keystate;
            unsigned long long Key;
        };
        struct MouseEvent{
            enum class Type {Wheel, Click, Move} Type;
            KeyState KeyState;
            MouseButton Button;
            float Wheel;
            vec2 Delta;
            vec2 ScreenPosition;
        };
    
        Nano::Signal<void(KeyEvent)> KeySignal;
        Nano::Signal<void(MouseEvent)> MouseSignal;
    
        void ProcessWindowsEvent(UINT message, WPARAM wParam, LPARAM lParam);
        void ProcessWindowsRawInput(LPARAM hRawInput);
        void DispatchEvents();
        void ClearFrameState();

        bool IsKeyDown(unsigned long long key);
        bool IsMouseButtonDown(MouseButton button);
        ivec2 GetMouseDelta();
    };
}