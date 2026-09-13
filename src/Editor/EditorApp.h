#pragma once

// Editor as a library. An editor executable is EntryPoint.cpp plus this lib;
// the game arrives at runtime as <project>/bin/Game.dll.

#include "Engine.h"

namespace batap
{
struct EditorConfig
{
    WindowDesc window_{.title = "Batap Engine", .fpsInTitle = true, .transparent = true};
};

// Engine + World + App loop, with the top-level try/catch.
int runEditor(const EditorConfig& cfg = {});
}  // namespace batap
