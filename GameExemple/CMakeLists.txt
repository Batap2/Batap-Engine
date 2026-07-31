# The test game: first consumer of the engine that is not the editor.
# It only sees Batap_Engine's public interface — if something is missing
# here, the fix is to grow the engine API, never to reach into src/Editor.
add_executable(Batap_TestGame
  main.cpp
)

target_link_libraries(Batap_TestGame PRIVATE
  Batap_Engine
)
