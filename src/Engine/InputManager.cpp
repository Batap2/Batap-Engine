#include "InputManager.h"

#include <cstddef>
#include <iostream>
#include <string>

namespace batap
{
void InputManager::feed(KeyEvent e)
{
    if (e.Keystate == KeyState::Pressed)
    {
        // Les répétitions clavier de l'OS arrivent comme des Pressed : seule
        // la vraie transition compte pour KeysPressed.
        if (!KeysDown.contains(e.Key))
            KeysPressed.insert(e.Key);
        KeysDown.insert(e.Key);
    }
    else
    {
        KeysReleased.insert(e.Key);
        KeysDown.erase(e.Key);
    }
}

void InputManager::feed(MouseEvent e)
{
    switch (e.Type)
    {
        case MouseEvent::Type::Click: {
            const size_t btn = static_cast<size_t>(e.Button);
            if (e.KeyState == KeyState::Pressed)
            {
                if (!MouseButtonsDown.at(btn))
                    MouseButtonsPressed[btn] = true;
                MouseButtonsDown[btn] = true;
                std::cout << "Mclick " << std::to_string(btn) << "\n";
            }
            else
            {
                MouseButtonsReleased[btn] = true;
                MouseButtonsDown[btn] = false;
            }
            break;
        }
        case MouseEvent::Type::Move:
            MouseDeltaAccumulated += e.Delta;
            MousePosition = e.ScreenPosition;
            std::cout << MousePosition.x() << " " << MousePosition.y() << "\n";
            break;
        case MouseEvent::Type::Wheel:
            MouseWheelAccumulated += e.Wheel;
            break;
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

bool InputManager::down(Key key) const
{
    return KeysDown.contains(key);
}

bool InputManager::pressed(Key key) const
{
    return KeysPressed.contains(key);
}

bool InputManager::released(Key key) const
{
    return KeysReleased.contains(key);
}

bool InputManager::down(MouseButton button) const
{
    return MouseButtonsDown[static_cast<size_t>(button)];
}

bool InputManager::pressed(MouseButton button) const
{
    return MouseButtonsPressed[static_cast<size_t>(button)];
}

bool InputManager::released(MouseButton button) const
{
    return MouseButtonsReleased[static_cast<size_t>(button)];
}

v2i InputManager::mouseDelta() const
{
    return MouseDeltaAccumulated;
}

v2i InputManager::mousePos() const
{
    return MousePosition;
}

float InputManager::wheel() const
{
    return MouseWheelAccumulated;
}
}  // namespace batap
