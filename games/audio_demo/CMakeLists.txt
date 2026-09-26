add_executable(audio_demo main.cpp AudioDemoModule.cpp)
target_link_libraries(audio_demo PRIVATE kke_engine)
engine_copy_runtime_file(audio_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/audio_demo/game.json)
foreach(shader model.vert model_instanced.vert shadow_instanced.vert model.frag shadow.vert shadow.frag cube.vert cube.frag)
    engine_add_shader(audio_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# Everything it shows is built from boxes in code; no assets needed.
