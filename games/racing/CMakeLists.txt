# Racing (games/racing/README.md): oval, drift and drag races on Jolt's
# vehicle physics (kke/Vehicle.h), with dents, smoke and sparks.
kke_add_game(racing main.cpp RacingModule.cpp Race.cpp Driving.cpp Damage.cpp Lobby.cpp Net.cpp NetRace.cpp Hud.cpp Track.cpp Cars.cpp)
target_link_libraries(racing PRIVATE kke_engine)
engine_copy_runtime_file(racing ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/racing/game.json)
engine_copy_runtime_file(racing ${CMAKE_CURRENT_SOURCE_DIR}/ui/racing_hud.rml ui/racing_hud.rml)
# The tracks (Track.h): one data file each, read at startup.
file(GLOB RACING_TRACKS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tracks/*.yaml ${CMAKE_CURRENT_SOURCE_DIR}/tracks/*.yml
     ${CMAKE_CURRENT_SOURCE_DIR}/tracks/*.json)
foreach(track ${RACING_TRACKS})
    get_filename_component(track_name ${track} NAME)
    engine_copy_runtime_file(racing ${track} tracks/${track_name})
endforeach()
engine_copy_runtime_file(racing ${CMAKE_SOURCE_DIR}/games/rmlui_demo/ui/theme.rcss ui/theme.rcss)
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSans-Italic.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(racing ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader cube.vert cube.frag glow.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag
               rml_ui.vert rml_ui.frag rml_gradient.frag effect.vert effect_smoke.frag effect_spark.frag)
    engine_add_shader(racing ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The cars and props (Synty's POLYGON Street Racer) are found at runtime in
# assets/synty (or KKE_ASSETS_DIR); without them the cars are blocks.
