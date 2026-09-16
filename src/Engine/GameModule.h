#pragma once

// Boundary between a host (editor) and a game DLL — loaded from
// <project>/bin/Game.dll when a project opens, or forced with `--game X.dll`.
//
// The DLL carries its own copy of the engine code it reaches, and with it a
// second copy of the reflection globals (registry, fieldTypeSlot<M>,
// componentBitSlot<T>) — each binary's code is wired at link time to its
// own variables, and LoadLibrary merges nothing. Only pointers passed at
// runtime cross the boundary. Load sequence (GameModuleLoader::load):
//
// 1. Load a copy (X_loaded_<n>.dll) — Windows locks loaded modules and the
//    original must stay overwritable by the linker.
// 2. LoadLibrary runs the DLL's BATAP_COMPONENTs -> its registry fills
//    itself, with its own numbering.
// 3. batapGameEntry (only export) fills the DLL's own field type slots (the
//    host filled its copies, not these) and returns this struct.
// 4. importFrom copies the unknown ComponentTypes into the host registry
//    (their function pointers target DLL code); the DLL registry is never
//    read again.
// 5. importFrom also copies the host's GPU bits into the DLL's bit variables
//    via bitSlot_ — DLL code computes markDirty masks from them, and both
//    modules must agree.
// 6. The host patches drawUI on the imported fields' slots (by typeName).
//
// After that a single engine lives (host World, pools, registry); the DLL
// only contributes code. Traps: game_module.cpp must include
// GameComponents.h (the include *is* the registration); App::gameModule_ is
// declared before game_ (~Game is DLL code, must run before FreeLibrary);
// and it must call adoptHostImGui(*out) or the game draws no UI (see below).

#include "Game.h"
#include "Reflection/ComponentRegistry.h"

#include <cstddef>

struct ImGuiContext;

namespace batap
{
struct GameModuleAPI
{
    ComponentRegistry* registry_ = nullptr;
    Game* (*createGame_)() = nullptr;
    const char* gameExeName_ = nullptr;

    // Filled by the host before the call, not by the DLL.
    ImGuiContext* imguiContext_ = nullptr;
    void* (*imguiAlloc_)(size_t, void*) = nullptr;
    void (*imguiFree_)(void*, void*) = nullptr;
    void* imguiUserData_ = nullptr;
};

// ImGui keeps its context and its allocator in globals, and the DLL links its
// own copy of both: without this the game's widgets go into a context nobody
// draws, and its allocations cross heaps.
void adoptHostImGui(const GameModuleAPI& api);

inline constexpr const char* GameModuleEntryName = "batapGameEntry";
using GameModuleEntryFn = void (*)(GameModuleAPI*);
}  // namespace batap
