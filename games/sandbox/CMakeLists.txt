add_executable(sandbox main.cpp SandboxModule.cpp)
target_link_libraries(sandbox PRIVATE kke_engine)

engine_copy_runtime_file(sandbox ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/sandbox/game.json)
foreach(shader cube.vert cube.frag shadow.vert shadow.frag debug.vert debug.frag)
    engine_add_shader(sandbox ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# Asset packs are found at runtime (assets/synty/, KKE_ASSETS_DIR, or a
# folder typed into the Assets panel) — never copied or committed.
