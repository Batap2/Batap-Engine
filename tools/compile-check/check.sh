#!/usr/bin/env bash
# Syntax + type check of the engine on a non-Windows host.
#
# The Windows build uses clang-cl — the same clang frontend as this check —
# so what parses and type-checks here compiles there, short of the linker and
# of Windows-SDK specifics. D3D12 types come from the DirectX-Headers
# submodule (which officially supports non-Windows via wsl/winadapter.h); the
# few Windows-SDK headers it does not cover are stubbed in ./stubs.
#
# Usage:
#   tools/compile-check/check.sh              # every checkable engine TU
#   tools/compile-check/check.sh <files...>   # just these files
#
# Requires the header-only submodules:
#   git submodule update --init --depth 1 \
#     include/DirectX-Headers include/eigen include/emhash include/entt \
#     include/json include/magic_enum include/nano-signal-slot include/stb
set -u
cd "$(git rev-parse --show-toplevel)"

# Windows-only TUs: real Win32/DXGI/DCOMP/D3DCompiler calls, no stub can
# carry them and they never compile anywhere but Windows. Their *headers*
# are still checked transitively through every TU below.
EXCLUDES=(
  "src/Engine/Platform/"
  "src/Engine/WindowsUtils/"
  "src/Engine/DebugUtils.cpp"      # OutputDebugString & friends
  "src/Engine/InputManager.cpp"    # decodes WM_* / RAWINPUT
  "src/Engine/Engine.cpp"          # platformInit / window creation
  "src/Engine/Renderer/Renderer.cpp"
  "src/Engine/Renderer/ResourceManager.cpp"
  "src/Engine/Renderer/CommandQueue.cpp"
  "src/Engine/Renderer/Shaders.cpp"
  "src/Engine/Renderer/DescriptorHeapAllocator.cpp"
  "src/Engine/Importers/"          # needs the assimp submodule (heavy)
  "src/Editor/WindowsApp.cpp"      # WinMain / message pump
  "src/Editor/main.cpp"            # wWinMain entry point
)

INC=(
  -I src/Engine
  -I src/Editor
  -isystem tools/compile-check/stubs
  -isystem include
  -isystem include/json/single_include
  -isystem include/DirectX-Headers/include
  -isystem include/DirectX-Headers/include/wsl/stubs
  -isystem include/eigen
  -isystem include/imgui
  -isystem include/nano-signal-slot
  -isystem include/magic_enum/include
  -isystem include/entt/single_include
  -isystem include/emhash
  -isystem include/stb
)

DEFS=(
  -include tools/compile-check/stubs/prelude.h
  -DIMGUI_USER_CONFIG='"UI/imguiConfig.h"'
  -DBATAP_ROOT_DIR='""'
)

# The project's warning set (CMakeLists batap_warnings), minus two that the
# Windows build demonstrably does not enforce:
#  -Wpadded fires on structs that already compile there (EntityHandle, ...);
#  -Wpoison-system-directories is a macOS cross-compilation artifact.
WARN=(
  -Weverything -Werror
  -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-c++20-compat
  -Wno-reserved-macro-identifier -Wno-nonportable-system-include-path
  -Wno-microsoft-enum-value -Wno-reserved-identifier
  -Wno-language-extension-token -Wno-switch-enum -Wno-switch-default
  -Wno-unused-parameter -Wno-unused-variable
  -Wno-unsafe-buffer-usage-in-libc-call -Wno-missing-prototypes
  -Wno-missing-designated-field-initializers
  -Wno-padded -Wno-poison-system-directories
  # Itanium-ABI-only diagnostic: clang-cl (Microsoft ABI) never emits it.
  -Wno-weak-vtables
)

if [ "$#" -gt 0 ]; then
  FILES=("$@")
else
  FILES=()
  while IFS= read -r f; do
    for ex in "${EXCLUDES[@]}"; do
      case "$f" in "$ex"*) continue 2 ;; esac
    done
    FILES+=("$f")
  done < <(find src/Engine src/Editor GameExemple -name '*.cpp' | sort)
fi

fail=0
for f in "${FILES[@]}"; do
  if out=$(clang++ -std=c++20 -fsyntax-only -ferror-limit=6 \
           "${WARN[@]}" "${INC[@]}" "${DEFS[@]}" "$f" 2>&1); then
    printf 'ok   %s\n' "$f"
  else
    printf 'FAIL %s\n%s\n' "$f" "$out"
    fail=1
  fi
done
exit "$fail"
