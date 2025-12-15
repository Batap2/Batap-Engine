# Compilos
set(CMAKE_C_COMPILER   cl.exe)
set(CMAKE_CXX_COMPILER cl.exe)

# IMPORTANT : désactive cmcldeps pour RC (doit être défini AVANT le project()).
set(CMAKE_NINJA_CMCLDEPS_RC OFF CACHE BOOL "" FORCE)

# Optionnel mais conseillé : ne jamais lancer ccache sur RC
set(CMAKE_RC_COMPILER_LAUNCHER "" CACHE STRING "" FORCE)