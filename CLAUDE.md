# Batap-Engine

C++20 game engine, Vulkan only (DX12 is gone and not coming back — no cross-API
abstraction layer). Philosophy: prefer few dependencies and straightforward
code. A lib or an abstraction is fine when it clearly pays for itself — say so
and propose it rather than silently writing a homemade version. Roadmap and
open tasks live in `src/Engine/OBJECTIFS.md`.

## Build

```
cmake --preset msvc-relwithdebinfo        # configure (clang-cl + Ninja)
cmake --build build/relwithdebinfo        # build
```

Prefer `build_msvc.bat <preset>` (sets up the VS env via vswhere before
CMake — a bare `cmake --preset` outside a VS dev prompt corrupts the cache).
Binaries land in `build/relwithdebinfo/bin/` (`Batap_Editor.exe`,
`GameExemple.exe`, `GameExemple_Game.dll`). Presets: `msvc-debug`,
`msvc-debug-asan`, `msvc-release`, `msvc-relwithdebinfo`. Target map: comment
at the top of the root `CMakeLists.txt`. The editor loads a project's game
from `<project>/bin/Game.dll` and hot-reloads it when it changes
(`src/Engine/GameModule.h` documents the whole mechanism).

To visually verify a render change: run the editor with `--project <dir>` and
env var `BATAP_DUMP_FRAME=N` to dump frame N as `frame_dump.png`. Add
`--scene <file.btpl>` for real content and `--select <entity name>` to make the
inspector draw — it renders nothing without a selection. A running editor holds
`Batap_Editor.exe` open, so close it before building.

## Code conventions

- `struct` everywhere, never `class`.
- Members end with an underscore: `moveSpeed_`, `registry_`.
- Everything lives in `namespace batap`; every header starts with `#pragma once`.
- No comments by default. Three exceptions, in English, kept short: code
  genuinely hard to follow; a trap; something whose reason lives far from the
  code (an invariant held elsewhere, a constraint from another system). Never:
  API docs on declarations, file headers, purpose notes, restating the code.
  Re-scan every diff and strip the rest before finishing.
- Warnings are errors; `-Wunsafe-buffer-usage` stays on globally. Suppress it
  locally at API boundaries (push/ignore/pop), never globally.
- Naming: components `Foo_C` (plain data), systems `Foo_S` (logic), one file
  each under `src/Engine/Components/` and `src/Engine/Systems/`.

## Architecture notes

- ECS on entt. A component is a plain struct + `BATAP_COMPONENT(Foo_C, "jsonKey")`
  in its header — fields, JSON serialization and inspector UI are all derived
  from the struct via `Reflection/ComponentRegistry.h`. No per-component code
  elsewhere.
- Keep components trivially copyable where possible (flat values + handles, no
  `std::string`/`std::vector`) — hot-reload snapshots depend on it.
- C++/HLSL shared layouts live once in `src/Engine/Shaders/ShaderInterop.h`
  (compiled by both dxc and the engine). Never duplicate a GPU struct in a
  `.hlsl` file.
- GPU instance data: `Instance/InstanceManager.h` pools, dirty-marking uploads
  everything (not per component). GPU ids use swap-remove — unstable across
  frames, do not persist them.
- Serialization: `Serialization/EntitySerializer.cpp`. Components unknown to the
  registry are kept as JSON blobs (`UnknownComponents_C`) and re-emitted on save.
