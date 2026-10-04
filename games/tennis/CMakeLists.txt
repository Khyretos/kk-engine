# Tennis (games/tennis/README.md): singles and doubles with a FEMFX soft
# rubber ball, against the CPU, on one screen or online.
kke_add_game(tennis main.cpp TennisModule.cpp Play.cpp Scene.cpp Hud.cpp Lobby.cpp Center.cpp Net.cpp NetTennis.cpp Court.cpp Rules.cpp Shot.cpp Swing.cpp Strings.cpp Replay.cpp Bench.cpp Marks.cpp NetCloth.cpp Walk.cpp Ball.cpp Bot.cpp Body.cpp)
target_link_libraries(tennis PRIVATE kke_engine)
engine_copy_runtime_file(tennis ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/tennis/game.json)
engine_copy_runtime_file(tennis ${CMAKE_CURRENT_SOURCE_DIR}/ui/tennis_hud.rml ui/tennis_hud.rml)
engine_copy_runtime_file(tennis ${CMAKE_SOURCE_DIR}/games/rmlui_demo/ui/theme.rcss ui/theme.rcss)
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSans-Italic.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(tennis ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
# cloth/translucent_*: the see-through net and windscreens (drawTranslucent).
foreach(shader cube.vert cube.frag glow.frag cloth.frag translucent_absorb.frag translucent_light.frag impostor.vert impostor.frag
               shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag rml_ui.vert rml_ui.frag
               rml_gradient.frag)
    engine_add_shader(tennis ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The players' bodies and clips (Quaternius' Universal Animation Library,
# CC0) are found at runtime in assets/animations/ (or KKE_ANIMATIONS_DIR).
