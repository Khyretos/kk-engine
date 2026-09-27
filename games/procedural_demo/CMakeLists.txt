# Procedural animation demo (games/procedural_demo/README.md): creatures with
# no animation clips, walking on kke::ProceduralGait, hit reactions on Jolt
# joint motors (kke::ActiveRagdoll). Nothing to download: every creature is
# generated.
kke_add_game(procedural_demo main.cpp ProceduralDemoModule.cpp)
target_link_libraries(procedural_demo PRIVATE kke_engine)
engine_copy_runtime_file(procedural_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/procedural_demo/game.json)
foreach(shader cube.vert cube.frag glow.frag shadow.vert shadow.frag)
    engine_add_shader(procedural_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
