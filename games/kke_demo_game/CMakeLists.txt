add_executable(kke_demo main.cpp CubeModule.cpp DestructionModule.cpp NetworkModule.cpp)
target_link_libraries(kke_demo PRIVATE kke_engine)

# Bundled default fonts (see README "Default fonts / font fallback chain")
# — copied next to the executable, same relative-path convention as shaders.
add_custom_command(TARGET kke_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/fonts/NotoSans-Regular.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/assets/fonts/NotoColorEmoji.ttf
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/fonts/NotoColorEmoji.ttf
)

# A minimal local "marketplace" directory for MarketplaceUiModule to scan
# at runtime — this game folder copying its own game.json into a
# marketplace/ directory next to the executable is a stand-in for "a real
# marketplace folder full of imported games" (see README "Game folder
# convention & marketplace"). Real multi-game discovery is still tracked
# as unstarted work; this is just enough for the demo to have something
# real to scan and render.
add_custom_command(TARGET kke_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/marketplace/kke_demo_game
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/game.json
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/marketplace/kke_demo_game/game.json
)

engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/cube.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/cube.frag)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/grid.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/grid.frag)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/particle.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/particle.frag)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/particle.comp)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/rml_ui.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/rml_ui.frag)
