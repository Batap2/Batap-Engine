#include "Shortcuts.h"

#include "Platform/PlatformWindow.h"

namespace batap
{
namespace
{
std::string withModifiers(const KeyChord& chord, const std::string& key)
{
    if (chord.key_ == Key::Unknown)
        return {};
    std::string text;
    if (chord.ctrl_)
        text += "Ctrl+";
    if (chord.shift_)
        text += "Shift+";
    return text + key;
}
}  // namespace

Shortcuts::Shortcuts()
{
    for (size_t i = 0; i < kShortcuts.size(); ++i)
        chords_[i] = kShortcuts[i].default_;
}

std::string Shortcuts::label(Shortcut s) const
{
    return chordLabel((*this)[s]);
}

std::string chordLabel(const KeyChord& chord)
{
    return withModifiers(chord, platformKeyLabel(chord.key_));
}

std::string chordText(const KeyChord& chord)
{
    return withModifiers(chord, keyName(chord.key_));
}

std::optional<KeyChord> parseChord(std::string_view text)
{
    KeyChord chord;
    if (text.empty())
        return chord;
    for (;;)
    {
        if (text.starts_with("Ctrl+"))
        {
            chord.ctrl_ = true;
            text.remove_prefix(5);
        }
        else if (text.starts_with("Shift+"))
        {
            chord.shift_ = true;
            text.remove_prefix(6);
        }
        else
            break;
    }
    chord.key_ = keyFromName(text);
    if (chord.key_ == Key::Unknown)
        return std::nullopt;
    return chord;
}
}  // namespace batap
