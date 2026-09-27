# Pet Companion (games/pet_companion/README.md): command a dog with the
# engine's orders (docs/COMMANDS.md).
add_executable(pet_companion main.cpp PetModule.cpp)
target_link_libraries(pet_companion PRIVATE kke_command_kit)
target_compile_definitions(pet_companion PRIVATE
    GAME_SCRIPTS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    GAME_SCRIPTS_INSTALLED="games/pet_companion/scripts")
engine_copy_runtime_file(pet_companion ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/pet_companion/game.json)
engine_copy_runtime_file(pet_companion ${CMAKE_CURRENT_SOURCE_DIR}/scripts/pet.lua games/pet_companion/scripts/pet.lua)
command_kit_game(pet_companion)
