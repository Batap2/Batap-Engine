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
# These publish their headers as non-SYSTEM includes; our warnings + -Werror
# would turn every one of their warnings into our build error. A pragma around
# the #include is not enough: -Wunsafe-buffer-usage ignores it.
foreach(_dep IN ITEMS Jolt vk-bootstrap)
  get_target_property(_dep_includes ${_dep} INTERFACE_INCLUDE_DIRECTORIES)
  set_target_properties(${_dep} PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_dep_includes}")
endforeach()

# ------------------------------------------------------------------------------
# Common warnings target
# ------------------------------------------------------------------------------
# An explicit list, never -Weverything: under -Werror, every clang release that
# adds a warning to -Weverything would break a build whose code did not change.
option(BATAP_WERROR "Treat warnings as errors" ON)

# clang-cl reads -Wall as MSVC's /Wall, which it maps to -Weverything;
# /W4 is its spelling of -Wall -Wextra.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  set(batap_base_warnings /W4)
else()
  set(batap_base_warnings -Wall -Wextra)
endif()

add_library(batap_warnings INTERFACE)
target_compile_options(batap_warnings INTERFACE
  ${batap_base_warnings}
  -Wpedantic
  -Wconversion
  -Wdouble-promotion
  -Wfloat-equal
  -Wshadow-all
  -Wold-style-cast
  -Wcast-align
  -Wcast-qual
  -Wnon-virtual-dtor
  -Wsuggest-override
  -Winconsistent-missing-destructor-override
  -Wdeprecated
  -Wimplicit-fallthrough
  -Wnull-dereference
  -Wundef
  -Wformat=2
  -Wmissing-noreturn
  -Wunreachable-code-aggressive
  -Wcomma
  -Wzero-as-null-pointer-constant
  -Wmissing-variable-declarations
  -Wunused-member-function
  -Wunused-template
  -Wunused-exception-parameter
  -Wloop-analysis
  -Wheader-hygiene
  -Wduplicate-enum
  -Wnewline-eof
  -Wextra-semi
  -Wextra-semi-stmt
  -Wcovered-switch-default
  -Wdocumentation
  -Wdocumentation-unknown-command
  -Wweak-vtables
  -Wexit-time-destructors
  -Wglobal-constructors
  -Wctad-maybe-unsupported
  -Wundefined-func-template
  -Wno-unused-parameter
  -Wno-unused-variable
  -Wno-microsoft-enum-value
  -Wno-language-extension-token
  $<$<BOOL:${BATAP_WERROR}>:-Werror>
)

# Warnings younger than the oldest supported clang:
# passed only when the compiler knows them, since -Werror turns an unknown
# warning option into an error. A "no-" entry is probed through its positive
# form, which is the one clang reports as unknown.
include(CheckCXXCompilerFlag)
function(batap_optional_warnings)
  foreach(warning IN LISTS ARGN)
    string(REGEX REPLACE "^no-" "" probed "${warning}")
    string(MAKE_C_IDENTIFIER "BATAP_HAS_W${probed}" has_warning)
    set(CMAKE_REQUIRED_FLAGS "-Werror=unknown-warning-option")
    check_cxx_compiler_flag("-W${probed}" ${has_warning})
    if(${has_warning})
      target_compile_options(batap_warnings INTERFACE "-W${warning}")
    endif()
  endforeach()
endfunction()
batap_optional_warnings(
  unsafe-buffer-usage
  no-unsafe-buffer-usage-in-libc-call
  cast-function-type-strict
  unique-object-duplication
  no-missing-designated-field-initializers
)

if(APPLE)
  # Parité avec tools/compile-check : diagnostics que le build Windows
  # applique mais pas encore le build macOS
  target_compile_options(batap_warnings INTERFACE
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