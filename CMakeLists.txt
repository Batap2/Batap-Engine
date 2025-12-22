cmake_minimum_required (VERSION 3.19)

# Enable Hot Reload for MSVC compilers if supported. /ZI
# if (POLICY CMP0141)
#   cmake_policy(SET CMP0141 NEW)
#   set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
# endif()

# Debug symbols without Edit&Continue (fixes Tracy / constexpr __LINE__)
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
endif()

project ("RayVox-Engine")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Définir le répertoire des sources et include
set(source_dir "${PROJECT_SOURCE_DIR}/src")
set(include_dir "${PROJECT_SOURCE_DIR}/include")

# Définir le répertoire de sortie des exécutables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)

option ( TRACY_ENABLE "" ON )
option ( TRACY_CALLSTACK "" ON )

add_subdirectory(${include_dir}/DirectX-Headers)
add_subdirectory(${include_dir}/assimp)
add_subdirectory(${include_dir}/tracy)

# Ajouter les fichiers source
file(GLOB_RECURSE source_files CONFIGURE_DEPENDS
    "${source_dir}/*.cpp"
    "${source_dir}/*.c"
    "${source_dir}/*.h"
    "${source_dir}/*.hpp"
)

file(GLOB IMGUI_SOURCES
    "${include_dir}/imgui/imgui.cpp"
    "${include_dir}/imgui/imgui_draw.cpp"
    "${include_dir}/imgui/imgui_tables.cpp"
    "${include_dir}/imgui/imgui_widgets.cpp"
    "${include_dir}/imgui/imgui_demo.cpp"
    "${include_dir}/imgui/backends/imgui_impl_win32.cpp"
    "${include_dir}/imgui/backends/imgui_impl_dx12.cpp"
)

set_source_files_properties(${IMGUI_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wno-error;-Wno-c++98-compat;-Wno-c++98-compat-pedantic"
  )

list(APPEND source_files ${IMGUI_SOURCES})

foreach(f IN LISTS IMGUI_SOURCES)
    if (f MATCHES "^${include_dir}/")
      set_source_files_properties(${f} PROPERTIES
        COMPILE_OPTIONS "-w"
      )
    endif()
  endforeach()

# Ajouter l'exécutable avec les fichiers source
add_executable(RayVox_Engine WIN32 ${source_files}
        resources.rc
        )

add_library(rayvox_warnings INTERFACE)

target_compile_options(rayvox_warnings INTERFACE
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
    #-Wno-unsafe-buffer-usage
    -Wno-unsafe-buffer-usage-in-libc-call
)

#Tracy::TracyClient
target_link_libraries(RayVox_Engine PRIVATE rayvox_warnings d3d12.lib dxgi.lib dxguid.lib D3DCompiler.lib assimp)

#target_include_directories(RayVox_Engine PRIVATE ${source_dir})
#target_precompile_headers(RayVox_Engine PRIVATE ${source_dir}/TracyInclude.h)


target_include_directories(RayVox_Engine PRIVATE
  ${source_dir}
)

target_include_directories(RayVox_Engine SYSTEM PRIVATE
    ${include_dir}
    ${include_dir}/DirectX-Headers/include
    ${include_dir}/eigen
    ${include_dir}/imgui
    ${include_dir}/nano-signal-slot
    ${include_dir}/magic_enum/include
    ${include_dir}/entt/single_include
)

target_include_directories(RayVox_Engine SYSTEM PRIVATE
  ${include_dir}/assimp/include
)

# Définir _DEBUG pour les builds Debug
target_compile_definitions(RayVox_Engine
        PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
)

target_compile_definitions(RayVox_Engine PRIVATE
    RAYVOX_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

# Définir les drapeaux de compilation
target_compile_options(RayVox_Engine PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O3>
)

target_compile_options(assimp PRIVATE
  $<$<CXX_COMPILER_ID:Clang>:-w>
)


add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMENT "Copy compile_commands.json to project root"
)