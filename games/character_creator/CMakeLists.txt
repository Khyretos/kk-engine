# Character creator: a realistic MakeHuman body made with sliders, saved,
# and shared as a short code (README.md, docs/HUMANS.md).
kke_add_game(character_creator main.cpp)
target_link_libraries(character_creator PRIVATE kke_engine)
engine_copy_runtime_file(character_creator ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/character_creator/game.json)
kke_use_character_creator(character_creator)
