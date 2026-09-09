add_executable(physics_demo main.cpp)
target_link_libraries(physics_demo PRIVATE kke_engine)

# engine_copy_runtime_file is shared across every game's CMakeLists.txt
# — see its definition in the root CMakeLists.txt for why (a real
# Ninja "multiple rules generate the same output" conflict, found by
# hitting it, when this game and kke_demo_game each had their own
# separately-named copy of this same pattern).
engine_copy_runtime_file(physics_demo ${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf assets/fonts/NotoSans-Regular.ttf)
engine_copy_runtime_file(physics_demo ${CMAKE_SOURCE_DIR}/assets/fonts/NotoColorEmoji.ttf assets/fonts/NotoColorEmoji.ttf)
engine_copy_runtime_file(physics_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/physics_demo/game.json)

# cube.vert/frag: reused directly by PhysicsModule for its own render
# bridge (see PhysicsModule.cpp's own comment on why — they already do
# exactly what a physics-driven mesh needs, no new shaders required).
# rml_ui.vert/frag: UiModule's real Vulkan text rendering.
engine_add_shader(physics_demo ${CMAKE_SOURCE_DIR}/shaders/cube.vert)
engine_add_shader(physics_demo ${CMAKE_SOURCE_DIR}/shaders/cube.frag)
engine_add_shader(physics_demo ${CMAKE_SOURCE_DIR}/shaders/shadow.vert)
engine_add_shader(physics_demo ${CMAKE_SOURCE_DIR}/shaders/shadow.frag)
engine_add_shader(physics_demo ${CMAKE_SOURCE_DIR}/shaders/rml_ui.vert)
engine_add_shader(physics_demo ${CMAKE_SOURCE_DIR}/shaders/rml_ui.frag)
