# batap_add_game(<name> EXE <sources...> MODULE <sources...>)
#
# Declares a game from the calling CMakeLists.txt, whose directory is the
# project the editor opens:
#   <name>       exe: the game standalone. Placed next to Batap_Editor.exe,
#                where the editor's Run button spawns it from.
#   <name>_Game  dll: the same game for the editor, copied after each build
#                (hot reload) to <project>/bin/Game.dll — or bin/Gamed.dll in
#                Debug, since a debug module cannot load under a release
#                editor and vice versa. The host picks the name matching its
#                own configuration (GameModuleFileName, GameModule.h).
function(batap_add_game name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "EXE;MODULE")
  if(NOT arg_EXE OR NOT arg_MODULE)
    message(FATAL_ERROR "batap_add_game(${name}): EXE and MODULE sources are both required")
  endif()
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "batap_add_game(${name}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()

  add_executable(${name} ${arg_EXE})
  add_library(${name}_Game SHARED ${arg_MODULE})

  foreach(target IN ITEMS ${name} ${name}_Game)
    target_link_libraries(${target} PRIVATE Batap_Engine)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  endforeach()

  add_custom_command(TARGET ${name}_Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${name}_Game>
            "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<IF:$<CONFIG:Debug>,Gamed,Game>.dll")
endfunction()
