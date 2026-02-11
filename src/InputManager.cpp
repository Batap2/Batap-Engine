#include "InputManager.h"

#include <windowsx.h>
#include <wrl.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <print>

#include "magic_enum/magic_enum.hpp"

namespace batap
{

static const std::array<Key, 256> VkToKey = []
{
    std::array<Key, 256> t{};
    t.fill(Key::Unknown);

    t['A'] = Key::A;
    t['B'] = Key::B;
    t['C'] = Key::C;
    t['D'] = Key::D;
    t['E'] = Key::E;
    t['F'] = Key::F;
    t['G'] = Key::G;
    t['H'] = Key::H;
    t['I'] = Key::I;
    t['J'] = Key::J;
    t['K'] = Key::K;
    t['L'] = Key::L;
    t['M'] = Key::M;
    t['N'] = Key::N;
    t['O'] = Key::O;
    t['P'] = Key::P;
    t['Q'] = Key::Q;
    t['R'] = Key::R;
    t['S'] = Key::S;
    t['T'] = Key::T;
    t['U'] = Key::U;
    t['V'] = Key::V;
    t['W'] = Key::W;
    t['X'] = Key::X;
    t['Y'] = Key::Y;
    t['Z'] = Key::Z;

    t['0'] = Key::Num0;
    t['1'] = Key::Num1;
    t['2'] = Key::Num2;
    t['3'] = Key::Num3;
    t['4'] = Key::Num4;
    t['5'] = Key::Num5;
    t['6'] = Key::Num6;
    t['7'] = Key::Num7;
    t['8'] = Key::Num8;
    t['9'] = Key::Num9;

    t[VK_F1] = Key::F1;
    t[VK_F2] = Key::F2;
    t[VK_F3] = Key::F3;
    t[VK_F4] = Key::F4;
    t[VK_F5] = Key::F5;
    t[VK_F6] = Key::F6;
    t[VK_F7] = Key::F7;
    t[VK_F8] = Key::F8;
    t[VK_F9] = Key::F9;
    t[VK_F10] = Key::F10;
    t[VK_F11] = Key::F11;
    t[VK_F12] = Key::F12;
    t[VK_F13] = Key::F13;
    t[VK_F14] = Key::F14;
    t[VK_F15] = Key::F15;
    t[VK_F16] = Key::F16;
    t[VK_F17] = Key::F17;
    t[VK_F18] = Key::F18;
    t[VK_F19] = Key::F19;
    t[VK_F20] = Key::F20;
    t[VK_F21] = Key::F21;
    t[VK_F22] = Key::F22;
    t[VK_F23] = Key::F23;
    t[VK_F24] = Key::F24;

    t[VK_SHIFT] = Key::LShift;
    t[VK_CONTROL] = Key::LCtrl;
    t[VK_MENU] = Key::LAlt;  // Alt
    t[VK_LWIN] = Key::LSuper;
    t[VK_RWIN] = Key::RSuper;

    t[VK_ESCAPE] = Key::Escape;
    t[VK_RETURN] = Key::Enter;
    t[VK_SPACE] = Key::Space;
    t[VK_TAB] = Key::Tab;
    t[VK_BACK] = Key::Backspace;

    t[VK_INSERT] = Key::Insert;
    t[VK_DELETE] = Key::Delete;
    t[VK_HOME] = Key::Home;
    t[VK_END] = Key::End;
    t[VK_PRIOR] = Key::PageUp;
    t[VK_NEXT] = Key::PageDown;

    t[VK_UP] = Key::ArrowUp;
    t[VK_DOWN] = Key::ArrowDown;
    t[VK_LEFT] = Key::ArrowLeft;
    t[VK_RIGHT] = Key::ArrowRight;

    t[VK_OEM_MINUS] = Key::Minus;
    t[VK_OEM_PLUS] = Key::Equal;
    t[VK_OEM_4] = Key::LeftBracket;
    t[VK_OEM_6] = Key::RightBracket;
    t[VK_OEM_5] = Key::Backslash;
    t[VK_OEM_1] = Key::Semicolon;
    t[VK_OEM_7] = Key::Apostrophe;
    t[VK_OEM_3] = Key::Grave;
    t[VK_OEM_COMMA] = Key::Comma;
    t[VK_OEM_PERIOD] = Key::Period;
    t[VK_OEM_2] = Key::Slash;

    t[VK_NUMPAD0] = Key::Numpad0;
    t[VK_NUMPAD1] = Key::Numpad1;
    t[VK_NUMPAD2] = Key::Numpad2;
    t[VK_NUMPAD3] = Key::Numpad3;
    t[VK_NUMPAD4] = Key::Numpad4;
    t[VK_NUMPAD5] = Key::Numpad5;
    t[VK_NUMPAD6] = Key::Numpad6;
    t[VK_NUMPAD7] = Key::Numpad7;
    t[VK_NUMPAD8] = Key::Numpad8;
    t[VK_NUMPAD9] = Key::Numpad9;

    t[VK_ADD] = Key::NumpadAdd;
    t[VK_SUBTRACT] = Key::NumpadSubtract;
    t[VK_MULTIPLY] = Key::NumpadMultiply;
    t[VK_DIVIDE] = Key::NumpadDivide;
    t[VK_DECIMAL] = Key::NumpadDecimal;

    // --- Locks ---
    t[VK_CAPITAL] = Key::CapsLock;
    t[VK_NUMLOCK] = Key::NumLock;
    t[VK_SCROLL] = Key::ScrollLock;

    // --- System ---
    t[VK_SNAPSHOT] = Key::PrintScreen;
    t[VK_PAUSE] = Key::Pause;
    t[VK_APPS] = Key::Menu;

    return t;
}();

void InputManager::ProcessWindowsEvent(uint32_t message, uintptr_t wParam, intptr_t lParam)
{
    switch (message)
    {
        case WM_KEYDOWN:
            if (!KeysDown.contains(VkToKey[wParam]))
                KeysPressed.insert(VkToKey[wParam]);
            KeysDown.insert(VkToKey[wParam]);
            break;

        case WM_KEYUP:
            KeysReleased.insert(VkToKey[wParam]);
            KeysDown.erase(VkToKey[wParam]);
            break;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            size_t btn = (message == WM_LBUTTONDOWN) ? 0 : (message == WM_RBUTTONDOWN) ? 1 : 2;
            if (!MouseButtonsDown.at(btn))
                MouseButtonsPressed[btn] = true;
            MouseButtonsDown[btn] = true;
            break;
        }

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            size_t btn = (message == WM_LBUTTONUP) ? 0 : (message == WM_RBUTTONUP) ? 1 : 2;
            MouseButtonsReleased[btn] = true;
            MouseButtonsDown[btn] = false;
            break;
        }

        case WM_MOUSEWHEEL:
            MouseWheelAccumulated += GET_WHEEL_DELTA_WPARAM(wParam) / 120.0f;
            break;

        case WM_MOUSEMOVE:
            MousePosition.x() = GET_X_LPARAM(lParam);
            MousePosition.y() = GET_Y_LPARAM(lParam);
            break;
    }
}

void InputManager::ProcessWindowsRawInput(intptr_t lParam)
{
    static constexpr UINT staticBufferSize = 256;
    static std::array<std::byte, staticBufferSize> staticBuffer;

    UINT bufferSize = staticBufferSize;
    std::byte* bufferPtr = staticBuffer.data();

    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, bufferPtr, &bufferSize,
                    sizeof(RAWINPUTHEADER));

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(bufferPtr);

    if (raw->header.dwType == RIM_TYPEMOUSE)
    {
        MouseDeltaAccumulated.x() += raw->data.mouse.lLastX;
        MouseDeltaAccumulated.y() += raw->data.mouse.lLastY;
    }
}

void InputManager::DispatchEvents()
{
    for (auto key : KeysPressed)
    {
        KeySignal.fire(KeyEvent{KeyState::Pressed, key});
    }

    for (auto key : KeysReleased)
    {
        KeySignal.fire(KeyEvent{KeyState::Released, key});
    }

    for (size_t i = 0; i < 3; i++)
    {
        if (MouseButtonsPressed[i])
        {
            MouseEvent e;
            e.Type = MouseEvent::Type::Click;
            e.KeyState = KeyState::Pressed;
            e.Button = static_cast<MouseButton>(i);
            e.ScreenPosition = MousePosition;
            MouseSignal.fire(e);
        }

        if (MouseButtonsReleased[i])
        {
            MouseEvent e;
            e.Type = MouseEvent::Type::Click;
            e.KeyState = KeyState::Released;
            e.Button = static_cast<MouseButton>(i);
            e.ScreenPosition = MousePosition;
            MouseSignal.fire(e);
        }
    }

    if (MouseDeltaAccumulated.x() != 0 || MouseDeltaAccumulated.y() != 0)
    {
        MouseEvent e;
        e.Type = MouseEvent::Type::Move;
        e.Delta = MouseDeltaAccumulated;
        e.ScreenPosition = MousePosition;
        MouseSignal.fire(e);
    }

    if (MouseWheelAccumulated != 0.0f)
    {
        MouseEvent e;
        e.Type = MouseEvent::Type::Wheel;
        e.Wheel = MouseWheelAccumulated;
        e.ScreenPosition = MousePosition;
        MouseSignal.fire(e);
    }
}

void InputManager::ClearFrameState()
{
    KeysPressed.clear();
    KeysReleased.clear();

    MouseButtonsPressed[0] = MouseButtonsPressed[1] = MouseButtonsPressed[2] = false;
    MouseButtonsReleased[0] = MouseButtonsReleased[1] = MouseButtonsReleased[2] = false;

    MouseDeltaAccumulated = {0, 0};
    MouseWheelAccumulated = 0.0f;
}

bool InputManager::IsKeyDown(Key key)
{
    return KeysDown.contains(key);
}

bool InputManager::IsMouseButtonDown(MouseButton button)
{
    return MouseButtonsDown[static_cast<size_t>(button)];
}

v2i InputManager::GetMouseDelta()
{
    return MouseDeltaAccumulated;
}
}  // namespace batap
