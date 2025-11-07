cmake_minimum_required (VERSION 3.19)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("RayVox-Engine")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# Définir le répertoire des sources et include
set(source_dir "${PROJECT_SOURCE_DIR}/src")
set(include_dir "${PROJECT_SOURCE_DIR}/include")

# Définir le répertoire de sortie des exécutables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/build)

include_directories(${source_dir})
include_directories(${include_dir})
add_subdirectory(${include_dir}/DirectX-Headers)
include_directories(${include_dir}/eigen)
include_directories(${include_dir}/imgui)
include_directories(${include_dir}/nano-signal-slot)
include_directories(${include_dir}/magic_enum/include)
include_directories(${include_dir}/entt/single_include)


# Ajouter les fichiers source
file(GLOB_RECURSE source_files "${source_dir}/*.cpp")

file(GLOB IMGUI_SOURCES
    "${include_dir}/imgui/imgui.cpp"
    "${include_dir}/imgui/imgui_draw.cpp"
    "${include_dir}/imgui/imgui_tables.cpp"
    "${include_dir}/imgui/imgui_widgets.cpp"
    "${include_dir}/imgui/imgui_demo.cpp"
    "${include_dir}/imgui/backends/imgui_impl_win32.cpp"
    "${include_dir}/imgui/backends/imgui_impl_dx12.cpp"
)
list(APPEND source_files ${IMGUI_SOURCES})

# Ajouter l'exécutable avec les fichiers source
add_executable(RayVox_Engine WIN32 ${source_files}
        resources.rc
        )

target_link_libraries(RayVox_Engine d3d12.lib dxgi.lib dxguid.lib D3DCompiler.lib)

#target_include_directories(RayVox_Engine PRIVATE ${source_dir})


# Définir _DEBUG pour les builds Debug
target_compile_definitions(RayVox_Engine
        PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
)

# Définir les drapeaux de compilation
add_compile_options(/WX)
target_compile_options(RayVox_Engine PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/O2>
)

add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMENT "Copy compile_commands.json to project root"
)