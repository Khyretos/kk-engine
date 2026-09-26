add_executable(jiggle_demo main.cpp JiggleDemoModule.cpp)
target_link_libraries(jiggle_demo PRIVATE kke_engine)
engine_copy_runtime_file(jiggle_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/jiggle_demo/game.json)
foreach(shader model.vert model_instanced.vert shadow_instanced.vert model.frag cube.vert cube.frag glow.frag impostor.vert impostor.frag shadow.vert shadow.frag)
    engine_add_shader(jiggle_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The character comes from a Synty pack found at runtime (assets/synty/ or
# KKE_ASSETS_DIR) and the clips from assets/animations/UAL1_Standard.fbx;
# neither is copied here. The jelly scene needs nothing.
