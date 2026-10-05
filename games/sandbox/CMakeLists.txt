kke_add_game(sandbox main.cpp SandboxModule.cpp PlayScripting.cpp GraphEditor.cpp PlayPalette.cpp FormPanel.cpp Toys.cpp Walker.cpp)
target_link_libraries(sandbox PRIVATE kke_engine)
engine_copy_runtime_file(sandbox ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/sandbox/game.json)
# effect*: flames, smoke, sparks and chips (kke::ParticleEffects).
foreach(shader model.vert model_instanced.vert shadow_instanced.vert model.frag shadow.vert shadow.frag debug.vert debug.frag cube.vert cube.frag
               effect.vert effect_smoke.frag effect_spark.frag effect_glow.frag effect_flake.frag effect_ring.frag)
    engine_add_shader(sandbox ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The palette, the node graph editor and the menus are RmlUi: its fonts.
kke_use_ui(sandbox)
