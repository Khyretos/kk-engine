# Flying (games/flying_demo/README.md): stunt planes over a generated
# island, with gamepads, keyboard and mouse, or flight sticks.
kke_add_game(flying_demo main.cpp FlyingModule.cpp Flight.cpp Course.cpp Combat.cpp FlyNet.cpp Stunts.cpp World.cpp Planes.cpp Damage.cpp Players.cpp
              Net.cpp Hud.cpp)
target_link_libraries(flying_demo PRIVATE kke_engine)
engine_copy_runtime_file(flying_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/flying_demo/game.json)
engine_copy_runtime_file(flying_demo ${CMAKE_CURRENT_SOURCE_DIR}/ui/flying_hud.rml ui/flying_hud.rml)
engine_copy_runtime_file(flying_demo ${CMAKE_SOURCE_DIR}/games/rmlui_demo/ui/theme.rcss ui/theme.rcss)
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSans-Italic.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(flying_demo ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader cube.vert cube.frag glow.frag impostor.vert impostor.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag
               rml_ui.vert rml_ui.frag rml_gradient.frag effect.vert effect_smoke.frag effect_spark.frag)
    engine_add_shader(flying_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The plane is Synty's POLYGON Stunt Plane pack, found at runtime in
# KKE_ASSETS_DIR (never committed); without it the planes are built from boxes.
