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

# ------------------------------------------------------------------------------
# Third-party dependencies
# ------------------------------------------------------------------------------
add_subdirectory("${include_dir}/DirectX-Headers")
add_subdirectory("${include_dir}/assimp")
add_subdirectory("${include_dir}/tracy")

# ------------------------------------------------------------------------------
# Common warnings target
# ------------------------------------------------------------------------------
add_library(batap_warnings INTERFACE)
target_compile_options(batap_warnings INTERFACE
  -Weverything
  -Werror
  -Wno-c++98-compat
  -Wno-c++98-compat-pedantic
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
)

# Global compile defs (Windows)
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ------------------------------------------------------------------------------
# Subprojects
# ------------------------------------------------------------------------------
add_subdirectory("${source_dir}/Engine")
add_subdirectory("${source_dir}/Editor")

# ------------------------------------------------------------------------------
# Third-party warning tweaks
# ------------------------------------------------------------------------------
# Silence assimp
target_compile_options(assimp PRIVATE
  $<$<CXX_COMPILER_ID:Clang>:-w>
)

# ------------------------------------------------------------------------------
# Utilities
# ------------------------------------------------------------------------------
add_custom_target(copy_compile_commands ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_BINARY_DIR}/compile_commands.json"
          "${CMAKE_SOURCE_DIR}/compile_commands.json"
  COMMENT "Copy compile_commands.json to project root"
)