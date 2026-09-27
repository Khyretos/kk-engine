kke_add_game(sandbox main.cpp SandboxModule.cpp PlayScripting.cpp GraphEditor.cpp PlayPalette.cpp)
target_link_libraries(sandbox PRIVATE kke_engine)

engine_copy_runtime_file(sandbox ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/sandbox/game.json)
foreach(shader model.vert model_instanced.vert shadow_instanced.vert model.frag shadow.vert shadow.frag debug.vert debug.frag cube.vert cube.frag)
    engine_add_shader(sandbox ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# Asset packs are found at runtime (assets/synty/, KKE_ASSETS_DIR, or a
# folder typed into the Assets panel) — never copied or committed.
