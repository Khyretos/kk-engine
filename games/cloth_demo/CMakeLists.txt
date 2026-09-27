add_executable(cloth_demo main.cpp ClothDemoModule.cpp)
target_link_libraries(cloth_demo PRIVATE kke_engine)
engine_copy_runtime_file(cloth_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/cloth_demo/game.json)
foreach(shader cube.vert cube.frag glow.frag cloth.frag impostor.vert impostor.frag shadow.vert shadow.frag)
    engine_add_shader(cloth_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# Needs no assets: every scene is built from cloth and simple shapes.
