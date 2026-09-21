cmake_minimum_required(VERSION 3.19)

# ------------------------------------------------------------------------------
# Policies / toolchain behavior
# ------------------------------------------------------------------------------
# Debug symbols without Edit&Continue (fixes Tracy / constexpr __LINE__)
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
endif()

# ------------------------------------------------------------------------------
# Project
# ------------------------------------------------------------------------------
# Target map:
#   Batap_Engine     (lib, src/Engine)   the engine, depends on nothing
#   Batap_EditorLib  (lib, src/Editor)   the editor, links the engine
#   Batap_Editor     (exe)               the editor; opening a project loads
#                                        its <project>/bin/Game.dll (hot reload)
# A game lives in its own repo: it adds this directory with add_subdirectory()
# and declares its targets with batap_add_game() (cmake/BatapGame.cmake).
project(Batap LANGUAGES C CXX)

# ------------------------------------------------------------------------------
# Global settings (keep minimal; prefer target_* when possible)
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output dir for executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ------------------------------------------------------------------------------
# Paths (shared)
# ------------------------------------------------------------------------------
set(source_dir  "${PROJECT_SOURCE_DIR}/src")
set(include_dir "${PROJECT_SOURCE_DIR}/include")

# ------------------------------------------------------------------------------
# Options (project / third-party)
# ------------------------------------------------------------------------------
option(TRACY_ENABLE     "" ON)
option(TRACY_CALLSTACK  "" ON)

set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES      OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT          ON  CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL            OFF CACHE BOOL "" FORCE)

# Jolt's JPH_* defines and ISA flags (/arch:AVX2) are PUBLIC on the target:
# every consumer compiles with the same ones, so ODR holds by construction.
set(ENABLE_ALL_WARNINGS          OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS           OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL               OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12                 OFF CACHE BOOL "" FORCE)
# Shared, explicitly: the editor exe and the game DLL each embed their own
# static copy of Batap_Engine; a static Jolt would give two Factory and two
# JPH::Allocate (null on the DLL side). Do not rely on the BUILD_SHARED_LIBS
# assimp leaves in the cache.
set(JPH_BUILD_SHARED_LIBS        ON  CACHE BOOL "" FORCE)

# ------------------------------------------------------------------------------
# Third-party dependencies
# ------------------------------------------------------------------------------
# Le SDK Vulkan LunarG fournit les headers (volk, vk-bootstrap, VMA) et dxc.
# FindVulkan le localise via l'env VULKAN_SDK posé par l'installeur ; pour un
# SDK ailleurs, configurer avec -DVulkan_ROOT=<chemin du SDK>.
find_package(Vulkan)
if(NOT Vulkan_FOUND)
  message(FATAL_ERROR
    "Vulkan SDK not found. https://vulkan.lunarg.com")
endif()

# volk a besoin de savoir quelle surface de plateforme déclarer (l'équivalent
# du VK_USE_PLATFORM_* qu'on passerait à vulkan.h)
if(WIN32)
  set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
  add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
  set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_METAL_EXT)
  add_compile_definitions(VK_USE_PLATFORM_METAL_EXT)
endif()
add_subdirectory("${include_dir}/volk")
add_subdirectory("${include_dir}/vk-bootstrap")
# assimp : consommé par Importers/ (import éditeur)
add_subdirectory("${include_dir}/assimp")
add_subdirectory("${include_dir}/tracy")
add_subdirectory("${include_dir}/JoltPhysics/Build")
# Jolt publishes its headers as non-SYSTEM includes; -Weverything -Werror would
# turn every one of its warnings into our build error.
get_target_property(_jolt_includes Jolt INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(Jolt PROPERTIES
  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_jolt_includes}")

# ------------------------------------------------------------------------------
# Common warnings target
# ------------------------------------------------------------------------------
add_library(batap_warnings INTERFACE)
target_compile_options(batap_warnings INTERFACE
  -Weverything
  -Werror
  -Wno-c++98-compat
  -Wno-c++98-compat-pedantic
  -Wno-c++20-compat
  -Wno-reserved-macro-identifier
  -Wno-nonportable-system-include-path
  -Wno-microsoft-enum-value
  -Wno-reserved-identifier
  -Wno-language-extension-token
  -Wno-switch-enum
  -Wno-switch-default
  -Wno-unused-parameter
  -Wno-unused-variable
  -Wno-unsafe-buffer-usage-in-libc-call
  -Wno-missing-prototypes
  # -Wpadded fires on nearly any struct mixing types, in the engine as in the
  # game: silencing it is the only option short of reordering every struct for
  # a diagnostic that says nothing about correctness. It was already off on
  # Apple; the clang shipped with VS 18 emits it too.
  -Wno-padded
  # -Wnrvo fires on the idiomatic `return {}` on failure, `return local` on
  # success: clang gives up NRVO as soon as two return statements disagree.
  # Forcing a single exit would make four functions worse to save one move on
  # an error path. It is a codegen hint, not a correctness diagnostic.
  -Wno-nrvo
  -Wno-missing-designated-field-initializers
)
if(APPLE)
  # Parité avec tools/compile-check : diagnostics que le build Windows
  # n'applique pas, ou spécifiques à AppleClang
  target_compile_options(batap_warnings INTERFACE
    -Wno-poison-system-directories
    -Wno-weak-vtables
    -Wno-exit-time-destructors
    -Wno-global-constructors
    -Wno-covered-switch-default
    -Wno-float-equal
    -Wno-double-promotion
    -Wno-implicit-int-float-conversion
    -Wno-shorten-64-to-32
    -Wno-sign-conversion
    -Wno-implicit-int-conversion
    -Wno-old-style-cast
    -Wno-cast-align
    -Wno-cast-qual
    -Wno-shadow
    -Wno-shadow-field-in-constructor
    -Wno-documentation
    -Wno-documentation-unknown-command
    -Wno-newline-eof
    -Wno-extra-semi
    -Wno-extra-semi-stmt
    -Wno-ctad-maybe-unsupported
    -Wno-switch-default
    -Wno-c++20-extensions
  )
endif()

# ------------------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------------------
# Silence all warnings on source files that live under include/ (third-party).
function(silence_external_sources target)
    get_target_property(_srcs ${target} SOURCES)
    foreach(f IN LISTS _srcs)
        if(f MATCHES "^${include_dir}/")
            set_source_files_properties("${f}"
                TARGET_DIRECTORY ${target}
                PROPERTIES COMPILE_OPTIONS "-w")
        endif()
    endforeach()
endfunction()

include("${PROJECT_SOURCE_DIR}/cmake/BatapGame.cmake")

# ------------------------------------------------------------------------------
# Subprojects
# ------------------------------------------------------------------------------
add_subdirectory("${source_dir}/Engine")
add_subdirectory("${source_dir}/Editor")

silence_external_sources(Batap_Engine)
silence_external_sources(Batap_EditorLib)

# ------------------------------------------------------------------------------
# Third-party warning tweaks
# ------------------------------------------------------------------------------
# Silence assimp
if(TARGET assimp)
  target_compile_options(assimp PRIVATE
    $<$<CXX_COMPILER_ID:Clang>:-w>
  )
endif()

# ------------------------------------------------------------------------------
# Utilities
# ------------------------------------------------------------------------------
add_custom_target(copy_compile_commands ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_BINARY_DIR}/compile_commands.json"
          "${CMAKE_SOURCE_DIR}/compile_commands.json"
  COMMENT "Copy compile_commands.json to project root"
)