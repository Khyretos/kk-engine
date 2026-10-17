kke_add_game(anim_demo main.cpp AnimDemoModule.cpp)
target_link_libraries(anim_demo PRIVATE kke_engine)
engine_copy_runtime_file(anim_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/anim_demo/game.json)
foreach(shader model.vert model_instanced.vert shadow_instanced.vert model.frag cube.vert cube.frag glow.frag shadow.vert shadow.frag)
    engine_add_shader(anim_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The clips come from assets/animations/UAL1_Standard.fbx (and UAL2.fbx for
# the eight-way walk and run), found at runtime, never copied here.
kke_use_ui(anim_demo)
