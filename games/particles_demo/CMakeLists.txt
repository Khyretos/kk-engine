kke_add_game(particles_demo main.cpp ParticlesDemoModule.cpp)
target_link_libraries(particles_demo PRIVATE kke_engine)
engine_copy_runtime_file(particles_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/particles_demo/game.json)
foreach(shader cube.vert glow.frag shadow.vert shadow.frag sky.vert effect.vert effect_smoke.frag effect_spark.frag effect_glow.frag
               effect_flake.frag effect_ring.frag)
    engine_add_shader(particles_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
kke_use_ui(particles_demo)
