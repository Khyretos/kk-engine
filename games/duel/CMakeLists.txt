# Duel (games/duel/README.md): a one-on-one fist fight on kke::Combatant,
# against a sparring bot or a second player.
add_executable(duel main.cpp DuelModule.cpp SparringBot.cpp Body.cpp Hud.cpp)
target_link_libraries(duel PRIVATE kke_engine)
engine_copy_runtime_file(duel ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/duel/game.json)
engine_copy_runtime_file(duel ${CMAKE_CURRENT_SOURCE_DIR}/ui/duel_hud.rml ui/duel_hud.rml)
engine_copy_runtime_file(duel ${CMAKE_CURRENT_SOURCE_DIR}/data/boxer.yml data/boxer.yml)
engine_copy_runtime_file(duel ${CMAKE_SOURCE_DIR}/games/rmlui_demo/ui/theme.rcss ui/theme.rcss)
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(duel ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader cube.vert cube.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag
               rml_ui.vert rml_ui.frag rml_gradient.frag)
    engine_add_shader(duel ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The fighters (Quaternius' Universal Animation Library, CC0) are found at
# runtime in assets/animations/ (or KKE_ANIMATIONS_DIR); UAL 2's melee
# clips there too, or under KKE_ASSETS_DIR.
