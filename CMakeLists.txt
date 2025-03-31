cmake_minimum_required (VERSION 3.8)

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

add_subdirectory(${include_dir}/DirectX-Headers)
add_subdirectory(${include_dir}/glm)

include_directories(${include_dir})

# Ajouter les fichiers source
file(GLOB source_files "${source_dir}/*.cpp")

# Ajouter l'exécutable avec les fichiers source
add_executable(RayVox_Engine WIN32 ${source_files}
        src/DX12Context.cpp
        src/DX12ComputeContext.cpp
        src/App.cpp
        src/CommandQueue.cpp
        src/InputManager.cpp
        resources.rc
        src/VoxelDataStructs.cpp
        )

target_link_libraries(RayVox_Engine d3d12.lib dxgi.lib dxguid.lib D3DCompiler.lib)
target_link_libraries(RayVox_Engine glm::glm)

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

#target_link_directories(RayVox_Engine PUBLIC ${PROJECT_SOURCE_DIR}/include)
#target_include_directories(RayVox_Engine PUBLIC ${PROJECT_SOURCE_DIR}/include)