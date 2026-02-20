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
project(Batap-Engine LANGUAGES C CXX)

# ------------------------------------------------------------------------------
# Global settings (keep minimal; prefer target_* when possible)
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output dir for executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ------------------------------------------------------------------------------
# Paths
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
# Sources
# ------------------------------------------------------------------------------
file(GLOB_RECURSE source_files CONFIGURE_DEPENDS
  "${source_dir}/*.cpp"
  "${source_dir}/*.c"
  "${source_dir}/*.h"
  "${source_dir}/*.hpp"
)

set(IMGUI_SOURCES
  "${include_dir}/imgui/imgui.cpp"
  "${include_dir}/imgui/imgui_draw.cpp"
  "${include_dir}/imgui/imgui_tables.cpp"
  "${include_dir}/imgui/imgui_widgets.cpp"
  "${include_dir}/imgui/imgui_demo.cpp"
  "${include_dir}/imgui/backends/imgui_impl_win32.cpp"
  "${include_dir}/imgui/backends/imgui_impl_dx12.cpp"
)

list(APPEND source_files ${IMGUI_SOURCES})

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

add_executable(Batap_Engine WIN32
  ${source_files}
  resources.rc
)

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
  #-Wno-unsafe-buffer-usage
  -Wno-unsafe-buffer-usage-in-libc-call
  -Wno-missing-prototypes
)

# ------------------------------------------------------------------------------
# Target includes / defs / options / links
# ------------------------------------------------------------------------------

target_include_directories(Batap_Engine PRIVATE
  "${source_dir}"
)

target_include_directories(Batap_Engine SYSTEM PRIVATE
  "${include_dir}"
  "${include_dir}/DirectX-Headers/include"
  "${include_dir}/eigen"
  "${include_dir}/imgui"
  "${include_dir}/nano-signal-slot"
  "${include_dir}/magic_enum/include"
  "${include_dir}/entt/single_include"
  "${include_dir}/assimp/include"
)

# Définir _DEBUG pour les builds Debug
target_compile_definitions(Batap_Engine PRIVATE
  $<$<CONFIG:Debug>:_DEBUG>
  BATAP_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

target_compile_definitions(Batap_Engine PRIVATE
  IMGUI_USER_CONFIG="${source_dir}/UI/imguiConfig.h"
)

# Définir les drapeaux de compilation
target_compile_options(Batap_Engine PRIVATE
  $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:MSVC>>:/O2>
  $<$<AND:$<CONFIG:RelWithDebInfo>,$<CXX_COMPILER_ID:MSVC>>:/O2>
)

if(WIN32)
    add_compile_definitions(NOMINMAX)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)  # bonus: réduit la taille des headers Windows
endif()

# Link
target_link_libraries(Batap_Engine PRIVATE
  batap_warnings
  d3d12.lib
  dxgi.lib
  dxguid.lib
  D3DCompiler.lib
  assimp
)

# ------------------------------------------------------------------------------
# Per-file / per-third-party warning tweaks
# ------------------------------------------------------------------------------

set_source_files_properties(${IMGUI_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-Wno-error;-Wno-c++98-compat;-Wno-c++98-compat-pedantic"
)

# If the file comes from include_dir, just silence it fully
foreach(f IN LISTS IMGUI_SOURCES)
  if (f MATCHES "^${include_dir}/")
    set_source_files_properties(${f} PROPERTIES COMPILE_OPTIONS "-w")
  endif()
endforeach()

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
