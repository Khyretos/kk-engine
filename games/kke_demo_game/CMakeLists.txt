add_executable(kke_demo main.cpp CubeModule.cpp DestructionModule.cpp NetworkModule.cpp)
target_link_libraries(kke_demo PRIVATE kke_engine)

# Bundled default fonts (see README "Default fonts / font fallback chain")
# — copied next to the executable, same relative-path convention as
# shaders. engine_copy_runtime_file is shared across every game's
# CMakeLists.txt (see its own definition, root CMakeLists.txt, for why
# it's a shared function rather than one locally defined per game).
engine_copy_runtime_file(kke_demo ${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf assets/fonts/NotoSans-Regular.ttf)
engine_copy_runtime_file(kke_demo ${CMAKE_SOURCE_DIR}/assets/fonts/NotoColorEmoji.ttf assets/fonts/NotoColorEmoji.ttf)

# A minimal local "marketplace" directory for MarketplaceUiModule to scan
# at runtime — see README "Game folder convention & marketplace".
engine_copy_runtime_file(kke_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/kke_demo_game/game.json)

engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/cube.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/cube.frag)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/shadow.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/shadow.frag)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/grid.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/grid.frag)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/particle.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/particle.frag)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/particle.comp)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/rml_ui.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/rml_ui.frag)
