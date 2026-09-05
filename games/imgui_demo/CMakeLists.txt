add_executable(imgui_demo main.cpp ImGuiShowcaseModule.cpp)
target_link_libraries(imgui_demo PRIVATE kke_engine)

engine_copy_runtime_file(imgui_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/imgui_demo/game.json)

engine_add_shader(imgui_demo ${CMAKE_SOURCE_DIR}/shaders/grid.vert)
engine_add_shader(imgui_demo ${CMAKE_SOURCE_DIR}/shaders/grid.frag)
