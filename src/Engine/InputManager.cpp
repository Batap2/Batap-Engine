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

std::string keyName(Key key)
{
    const auto from = [key](Key first) { return static_cast<int>(key) - static_cast<int>(first); };
    if (key >= Key::A && key <= Key::Z)
        return std::string(1, static_cast<char>('A' + from(Key::A)));
    if (key >= Key::Num0 && key <= Key::Num9)
        return std::string(1, static_cast<char>('0' + from(Key::Num0)));
    if (key >= Key::F1 && key <= Key::F24)
        return "F" + std::to_string(1 + from(Key::F1));
    if (key >= Key::Numpad0 && key <= Key::Numpad9)
        return "Numpad " + std::to_string(from(Key::Numpad0));

    switch (key)
    {
        case Key::LShift:
            return "Left Shift";
        case Key::RShift:
            return "Right Shift";
        case Key::LCtrl:
            return "Left Ctrl";
        case Key::RCtrl:
            return "Right Ctrl";
        case Key::LAlt:
            return "Left Alt";
        case Key::RAlt:
            return "Right Alt";
        case Key::LSuper:
            return "Left Super";
        case Key::RSuper:
            return "Right Super";
        case Key::Escape:
            return "Escape";
        case Key::Enter:
            return "Enter";
        case Key::Space:
            return "Space";
        case Key::Tab:
            return "Tab";
        case Key::Backspace:
            return "Backspace";
        case Key::Insert:
            return "Insert";
        case Key::Delete:
            return "Delete";
        case Key::Home:
            return "Home";
        case Key::End:
            return "End";
        case Key::PageUp:
            return "Page Up";
        case Key::PageDown:
            return "Page Down";
        case Key::ArrowUp:
            return "Up";
        case Key::ArrowDown:
            return "Down";
        case Key::ArrowLeft:
            return "Left";
        case Key::ArrowRight:
            return "Right";
        case Key::Minus:
            return "-";
        case Key::Equal:
            return "=";
        case Key::LeftBracket:
            return "[";
        case Key::RightBracket:
            return "]";
        case Key::Backslash:
            return "\\";
        case Key::Semicolon:
            return ";";
        case Key::Apostrophe:
            return "'";
        case Key::Grave:
            return "`";
        case Key::Comma:
            return ",";
        case Key::Period:
            return ".";
        case Key::Slash:
            return "/";
        case Key::NumpadAdd:
            return "Numpad Add";
        case Key::NumpadSubtract:
            return "Numpad Subtract";
        case Key::NumpadMultiply:
            return "Numpad Multiply";
        case Key::NumpadDivide:
            return "Numpad Divide";
        case Key::NumpadDecimal:
            return "Numpad Decimal";
        case Key::NumpadEnter:
            return "Numpad Enter";
        case Key::CapsLock:
            return "Caps Lock";
        case Key::NumLock:
            return "Num Lock";
        case Key::ScrollLock:
            return "Scroll Lock";
        case Key::PrintScreen:
            return "Print Screen";
        case Key::Pause:
            return "Pause";
        case Key::Menu:
            return "Menu";
        default:
            return {};
    }
}

Key keyFromName(std::string_view name)
{
    for (int i = 1; i < static_cast<int>(Key::Count); ++i)
    {
        const auto key = static_cast<Key>(i);
        if (keyName(key) == name)
            return key;
    }
    return Key::Unknown;
}
}  // namespace batap
