add_executable(rmlui_demo main.cpp RmlUiShowcaseModule.cpp)
target_link_libraries(rmlui_demo PRIVATE kke_engine RmlUi::Debugger)

engine_copy_runtime_file(rmlui_demo ${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf assets/fonts/NotoSans-Regular.ttf)
engine_copy_runtime_file(rmlui_demo ${CMAKE_SOURCE_DIR}/assets/fonts/NotoColorEmoji.ttf assets/fonts/NotoColorEmoji.ttf)
engine_copy_runtime_file(rmlui_demo ${CMAKE_SOURCE_DIR}/assets/textures/test_icon.png assets/textures/test_icon.png)
engine_copy_runtime_file(rmlui_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/rmlui_demo/game.json)

engine_add_shader(rmlui_demo ${CMAKE_SOURCE_DIR}/shaders/rml_ui.vert)
engine_add_shader(rmlui_demo ${CMAKE_SOURCE_DIR}/shaders/rml_ui.frag)
