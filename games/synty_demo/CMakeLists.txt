add_executable(synty_demo main.cpp SyntySceneModule.cpp)
target_link_libraries(synty_demo PRIVATE kke_engine)

engine_copy_runtime_file(synty_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/synty_demo/game.json)
foreach(shader cube.vert cube.frag shadow.vert shadow.frag)
    engine_add_shader(synty_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The Synty pack itself is found at runtime (assets/synty/ or KKE_SYNTY_DIR)
# rather than copied here — it's large, and licensed per user.
