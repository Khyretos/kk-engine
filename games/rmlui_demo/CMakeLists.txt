add_executable(rmlui_demo main.cpp ShowcaseModule.cpp BackdropModule.cpp)
target_link_libraries(rmlui_demo PRIVATE kke_engine RmlUi::Debugger)

foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSans-Italic.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(rmlui_demo ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
engine_copy_runtime_file(rmlui_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/rmlui_demo/game.json)

# The UI itself: every .rml/.rcss/.png under ui/ is copied next to the
# executable, and re-copied whenever it changes. For live editing without
# rebuilding, set KKE_UI_ROOT to this source folder instead (see main.cpp).
file(GLOB_RECURSE KKE_SHOWCASE_UI_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/ui
     CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.rml ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.rcss ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.png)
foreach(ui_file ${KKE_SHOWCASE_UI_FILES})
    engine_copy_runtime_file(rmlui_demo ${CMAKE_CURRENT_SOURCE_DIR}/ui/${ui_file} ui/${ui_file})
endforeach()

foreach(shader cube.vert cube.frag shadow.vert shadow.frag rml_ui.vert rml_ui.frag rml_gradient.frag)
    engine_add_shader(rmlui_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
