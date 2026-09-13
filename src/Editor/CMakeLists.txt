file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)
# EntryPoint.cpp goes to the exe, not the lib: an object in a static lib whose
# symbols nothing references would be dropped by the linker.
list(REMOVE_ITEM EDITOR_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/EntryPoint.cpp"
)

set(IMGUI_STDLIB "${PROJECT_SOURCE_DIR}/include/imgui/misc/cpp/imgui_stdlib.cpp")

add_library(Batap_EditorLib STATIC
  ${EDITOR_SOURCES}
  "${IMGUI_STDLIB}"
)

target_include_directories(Batap_EditorLib PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"          # src/Editor
)

target_link_libraries(Batap_EditorLib PUBLIC
  Batap_Engine
)

if(WIN32)
  add_executable(Batap_Editor WIN32
    "${CMAKE_CURRENT_SOURCE_DIR}/EntryPoint.cpp"
    "${PROJECT_SOURCE_DIR}/resources.rc"
  )
else()
  add_executable(Batap_Editor "${CMAKE_CURRENT_SOURCE_DIR}/EntryPoint.cpp")
endif()

target_link_libraries(Batap_Editor PRIVATE Batap_EditorLib)
