#include "InputManager.h"

#include <Windowsx.h>

#include <cstddef>
#include <iostream>

#include "tracy/public/tracy/Tracy.hpp"

namespace rayvox
{

void InputManager::ProcessWindowsEvent(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_KEYDOWN:
            if (!KeysDown.contains(wParam))
                KeysPressed.insert(wParam);
            KeysDown.insert(wParam);
            break;

        case WM_KEYUP:
            KeysReleased.insert(wParam);
            KeysDown.erase(wParam);
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

void InputManager::ProcessWindowsRawInput(LPARAM lParam)
{
    static constexpr UINT staticBufferSize = 256;
    static std::array<std::byte, staticBufferSize> staticBuffer;

    UINT bufferSize = staticBufferSize;
    std::byte* bufferPtr = staticBuffer.data();

    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, bufferPtr,
                                  &bufferSize, sizeof(RAWINPUTHEADER));

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

bool InputManager::IsKeyDown(unsigned long long key)
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
}  // namespace rayvox