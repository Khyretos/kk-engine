kke_add_game(water_demo main.cpp WaterDemoModule.cpp)
target_link_libraries(water_demo PRIVATE kke_engine)
engine_copy_runtime_file(water_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/water_demo/game.json)
foreach(shader cube.vert glow.frag impostor.vert impostor.frag shadow.vert shadow.frag sky.vert fluid_depth.frag fluid_blur.comp fluid_composite.frag
               fluid_thickness.frag fluid_water.frag)
    engine_add_shader(water_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# Everything it shows is built from boxes in code; no assets needed.
kke_use_ui(water_demo)
