file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

set(IMGUI_STDLIB "${PROJECT_SOURCE_DIR}/include/imgui/misc/cpp/imgui_stdlib.cpp")

if(WIN32)
  # WIN32 = sous-système fenêtré (wWinMain) ; l'icône vient du .rc
  add_executable(Batap_Editor WIN32
    ${EDITOR_SOURCES}
    "${PROJECT_SOURCE_DIR}/resources.rc"
    "${IMGUI_STDLIB}"
  )
else()
  add_executable(Batap_Editor
    ${EDITOR_SOURCES}
    "${IMGUI_STDLIB}"
  )
endif()


target_include_directories(Batap_Editor PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"          # src/Editor
)

target_link_libraries(Batap_Editor PRIVATE
  Batap_Engine
)