# Goblin Horde (games/goblin_horde/README.md): hold a ruined fort against
# waves of goblins; crowd AI on the engine AI core, melee on kke::Combat.
add_executable(goblin_horde main.cpp HordeModule.cpp Hero.cpp Goblins.cpp Hud.cpp)
target_link_libraries(goblin_horde PRIVATE kke_engine)
engine_copy_runtime_file(goblin_horde ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/goblin_horde/game.json)
engine_copy_runtime_file(goblin_horde ${CMAKE_CURRENT_SOURCE_DIR}/ui/horde_hud.rml ui/horde_hud.rml)
engine_copy_runtime_file(goblin_horde ${CMAKE_CURRENT_SOURCE_DIR}/data/goblin.yml data/goblin.yml)
engine_copy_runtime_file(goblin_horde ${CMAKE_CURRENT_SOURCE_DIR}/data/waves.yml data/waves.yml)
engine_copy_runtime_file(goblin_horde ${CMAKE_SOURCE_DIR}/games/rmlui_demo/ui/theme.rcss ui/theme.rcss)
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(goblin_horde ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader cube.vert cube.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag
               rml_ui.vert rml_ui.frag rml_gradient.frag)
    engine_add_shader(goblin_horde ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The goblins (Synty SIDEKICK Goblin Fighters + ANIMATION Goblin Locomotion)
# and the king (POLYGON Fantasy Characters) are found at runtime under
# assets/synty or KKE_ASSETS_DIR; never committed (docs/SCENES.md).
