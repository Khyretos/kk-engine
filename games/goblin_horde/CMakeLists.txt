# Goblin Horde (games/goblin_horde/README.md): hold a ruined fort against
# waves of goblins; crowd AI on the engine AI core, melee on kke::Combat.
kke_add_game(goblin_horde main.cpp HordeModule.cpp Roster.cpp Art.cpp Puppets.cpp Heroes.cpp Foes.cpp Shots.cpp Hud.cpp Net.cpp)
target_link_libraries(goblin_horde PRIVATE kke_engine)
engine_copy_runtime_file(goblin_horde ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/goblin_horde/game.json)
engine_copy_runtime_file(goblin_horde ${CMAKE_CURRENT_SOURCE_DIR}/ui/horde_hud.rml ui/horde_hud.rml)
foreach(data goblin.yml waves.yml foes.yml heroes.yml)
    engine_copy_runtime_file(goblin_horde ${CMAKE_CURRENT_SOURCE_DIR}/data/${data} data/${data})
endforeach()
engine_copy_runtime_file(goblin_horde ${CMAKE_SOURCE_DIR}/games/rmlui_demo/ui/theme.rcss ui/theme.rcss)
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSans-Italic.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(goblin_horde ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader cube.vert cube.frag glow.frag impostor.vert impostor.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert
               model.frag rml_ui.vert rml_ui.frag rml_gradient.frag effect.vert effect_smoke.frag effect_spark.frag)
    engine_add_shader(goblin_horde ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The goblins (Synty POLYGON Goblin War Camp and Dungeon), the bosses
# (POLYGON Fantasy Rivals), the players (POLYGON Fantasy Characters, or the
# UAL mannequin) and their animations (Quaternius UAL 1 and 2) are found at
# runtime under assets/ or KKE_ASSETS_DIR; never committed (docs/SCENES.md).
