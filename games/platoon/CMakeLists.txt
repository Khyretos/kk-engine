# Platoon (games/platoon/README.md): command a squad with the engine's
# orders (docs/COMMANDS.md); soldiers on both sides are AI core agents.
add_executable(platoon main.cpp PlatoonModule.cpp)
target_link_libraries(platoon PRIVATE kke_command_kit)
target_compile_definitions(platoon PRIVATE
    GAME_SCRIPTS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    GAME_SCRIPTS_INSTALLED="games/platoon/scripts")
engine_copy_runtime_file(platoon ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/platoon/game.json)
engine_copy_runtime_file(platoon ${CMAKE_CURRENT_SOURCE_DIR}/scripts/platoon.lua games/platoon/scripts/platoon.lua)
command_kit_game(platoon)
