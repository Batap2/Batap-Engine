file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_executable(Batap_Editor WIN32
  ${EDITOR_SOURCES}
  "${PROJECT_SOURCE_DIR}/resources.rc"
)

target_include_directories(Batap_Editor PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"          # src/Editor
)

target_link_libraries(Batap_Editor PRIVATE
  Batap_Engine
)