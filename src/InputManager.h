#pragma once

#include <cstdint>
#include <unordered_set>

#include "EigenTypes.h"
#include "nano_signal_slot.hpp"

namespace batap
{
struct Context;

enum class Key : uint16_t
{
    Unknown = 0,

    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,

    LShift,
    RShift,
    LCtrl,
    RCtrl,
    LAlt,
    RAlt,
    LSuper,
    RSuper,  // Windows / Command

    Escape,
    Enter,
    Space,
    Tab,
    Backspace,

    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,

    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,

    Minus,
    Equal,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Grave,  // `
    Comma,
    Period,
    Slash,

    Numpad0,
    Numpad1,
    Numpad2,
    Numpad3,
    Numpad4,
    Numpad5,
    Numpad6,
    Numpad7,
    Numpad8,
    Numpad9,

    NumpadAdd,
    NumpadSubtract,
    NumpadMultiply,
    NumpadDivide,
    NumpadDecimal,
    NumpadEnter,

    CapsLock,
    NumLock,
    ScrollLock,

    PrintScreen,
    Pause,
    Menu,

    Count
};

enum class MouseButton
{
    Left = 0,
    Right = 1,
    Middle = 2
};

struct InputManager
{
    Context* Ctx;

    enum class KeyState
    {
        Pressed,
        Released
    };

    std::unordered_set<Key> KeysDown;
    std::unordered_set<Key> KeysPressed;
    std::unordered_set<Key> KeysReleased;
    std::array<bool, 3> MouseButtonsDown = {false};
    std::array<bool, 3> MouseButtonsPressed = {false};
    std::array<bool, 3> MouseButtonsReleased = {false};
    v2i MouseDeltaAccumulated = {0, 0};
    v2i MousePosition = {0, 0};
    float MouseWheelAccumulated = 0.0f;

    struct KeyEvent
    {
        KeyState Keystate;
        Key Key;
    };
    struct MouseEvent
    {
        enum class Type
        {
            Wheel,
            Click,
            Move
        } Type;
        KeyState KeyState;
        MouseButton Button;
        float Wheel;
        v2i Delta;
        v2i ScreenPosition;
    };

    Nano::Signal<void(KeyEvent)> KeySignal;
    Nano::Signal<void(MouseEvent)> MouseSignal;

    void ProcessWindowsEvent(uint32_t message, uintptr_t wParam, intptr_t lParam);
    void ProcessWindowsRawInput(intptr_t hRawInput);
    void DispatchEvents();
    void ClearFrameState();

    bool IsKeyDown(Key key);
    bool IsMouseButtonDown(MouseButton button);
    v2i GetMouseDelta();
};
}  // namespace batap
