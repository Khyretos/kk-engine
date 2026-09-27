kke_add_game(cloth_demo main.cpp ClothDemoModule.cpp)
target_link_libraries(cloth_demo PRIVATE kke_engine)
engine_copy_runtime_file(cloth_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/cloth_demo/game.json)
foreach(shader cube.vert cube.frag glow.frag cloth.frag impostor.vert impostor.frag shadow.vert shadow.frag hair.vert hair.frag hair_shadow.vert hair_points.comp)
    engine_add_shader(cloth_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# Needs no assets: every scene is built from cloth, hair and simple shapes.
kke_use_ui(cloth_demo)
