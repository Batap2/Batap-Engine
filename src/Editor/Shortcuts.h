#pragma once

#include "InputManager.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace batap
{
enum struct Shortcut
{
    Move,
    Rotate,
    Scale,
    ToggleSpace,
    TogglePivot,
    Focus,
    Rename,
    Duplicate,
    Delete,
    Save,
    SaveAs,
    OpenScene,
    Play,
    ContentBrowser,
    Count
};

struct KeyChord
{
    Key key_ = Key::Unknown;
    bool ctrl_ = false;
    bool shift_ = false;

    bool operator==(const KeyChord&) const = default;
};

struct ShortcutInfo
{
    const char* id_;
    const char* label_;
    KeyChord default_;
};

// Indexed by Shortcut.
inline constexpr std::array<ShortcutInfo, static_cast<size_t>(Shortcut::Count)> kShortcuts{{
    {"move", "Move", {Key::W}},
    {"rotate", "Rotate", {Key::E}},
    {"scale", "Scale", {Key::R}},
    {"toggleSpace", "Local / Global", {Key::X}},
    {"togglePivot", "Pivot / Center", {Key::Z}},
    {"focus", "Focus the selection", {Key::F}},
    {"rename", "Rename", {Key::F2}},
    {"duplicate", "Duplicate", {Key::D, true}},
    {"delete", "Delete", {Key::Delete}},
    {"save", "Save", {Key::S, true}},
    {"saveAs", "Save as", {Key::S, true, true}},
    {"openScene", "Open scene", {Key::O, true}},
    {"play", "Play / Stop", {Key::P, true}},
    {"contentBrowser", "Content browser", {Key::Space, true}},
}};

struct Shortcuts
{
    Shortcuts();

    KeyChord& operator[](Shortcut s) { return chords_[static_cast<size_t>(s)]; }
    const KeyChord& operator[](Shortcut s) const { return chords_[static_cast<size_t>(s)]; }
    std::string label(Shortcut s) const;

    std::array<KeyChord, kShortcuts.size()> chords_;
};

std::string chordLabel(const KeyChord& chord);
std::string chordText(const KeyChord& chord);
std::optional<KeyChord> parseChord(std::string_view text);
}  // namespace batap
