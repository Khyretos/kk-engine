kke_add_game(jiggle_demo main.cpp JiggleDemoModule.cpp)
target_link_libraries(jiggle_demo PRIVATE kke_engine)
engine_copy_runtime_file(jiggle_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/jiggle_demo/game.json)
foreach(shader model.vert model_instanced.vert shadow_instanced.vert model.frag cube.vert cube.frag glow.frag translucent_absorb.frag translucent_light.frag impostor.vert impostor.frag shadow.vert shadow.frag hair.vert hair.frag hair_shadow.vert hair_points.comp)
    engine_add_shader(jiggle_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The body: our CC0 Vitruvian woman, copied next to the executable with its licence note
# (the demo finds it with kke::findAssetFolder("assets/bodies")). The clips,
# assets/animations/UAL1_Standard.fbx, are copied by the engine library. The jelly scene needs nothing.
foreach(f vitruvian_voluptuous.glb LICENSE.txt)
    engine_copy_runtime_file(jiggle_demo ${CMAKE_SOURCE_DIR}/assets/bodies/${f} assets/bodies/${f})
endforeach()
kke_use_ui(jiggle_demo)
