# Climb Race (games/climb_race/README.md): a generated mountain, climbed hold
# by hold (kke::ClimbWall, kke::Climber), against a rival.
add_executable(climb_race main.cpp ClimbRaceModule.cpp Body.cpp Hud.cpp Lobby.cpp)
target_link_libraries(climb_race PRIVATE kke_engine)
engine_copy_runtime_file(climb_race ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/climb_race/game.json)
engine_copy_runtime_file(climb_race ${CMAKE_CURRENT_SOURCE_DIR}/ui/climb_hud.rml ui/climb_hud.rml)
engine_copy_runtime_file(climb_race ${CMAKE_SOURCE_DIR}/games/rmlui_demo/ui/theme.rcss ui/theme.rcss)
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSans-Italic.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(climb_race ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader cube.vert cube.frag glow.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag
               rml_ui.vert rml_ui.frag rml_gradient.frag)
    engine_add_shader(climb_race ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The climbers' bodies and clips (Quaternius' Universal Animation Library,
# CC0) are found at runtime in assets/animations/ (or KKE_ANIMATIONS_DIR).
