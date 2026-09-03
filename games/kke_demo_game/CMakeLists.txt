add_executable(kke_demo main.cpp CubeModule.cpp DestructionModule.cpp NetworkModule.cpp)
target_link_libraries(kke_demo PRIVATE kke_engine)

# Bundled default fonts (see README "Default fonts / font fallback chain")
# — copied next to the executable, same relative-path convention as shaders.
#
# OUTPUT-based custom commands with explicit DEPENDS, not POST_BUILD: a
# POST_BUILD command only runs when the target itself actually relinks,
# so editing a font/manifest file without touching any source file would
# silently NOT re-copy it on the next `cmake --build` — found exactly
# this bug while testing the hardware-requirements check (had to copy
# game.json by hand to verify a change took effect). This form makes
# Ninja/Make check each file's mtime against its DEPENDS every build,
# regardless of whether kke_demo's sources changed.
function(kke_copy_runtime_file SRC DEST_RELATIVE)
    set(output_path ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${DEST_RELATIVE})
    get_filename_component(output_dir ${output_path} DIRECTORY)
    string(MD5 target_suffix "${output_path}")
    add_custom_command(
        OUTPUT ${output_path}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SRC} ${output_path}
        DEPENDS ${SRC}
        COMMENT "Copying ${DEST_RELATIVE}"
    )
    add_custom_target(kke_copy_${target_suffix} DEPENDS ${output_path})
    add_dependencies(kke_demo kke_copy_${target_suffix})
endfunction()

kke_copy_runtime_file(${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf assets/fonts/NotoSans-Regular.ttf)
kke_copy_runtime_file(${CMAKE_SOURCE_DIR}/assets/fonts/NotoColorEmoji.ttf assets/fonts/NotoColorEmoji.ttf)

# A minimal local "marketplace" directory for MarketplaceUiModule to scan
# at runtime — see README "Game folder convention & marketplace".
kke_copy_runtime_file(${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/kke_demo_game/game.json)

engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/cube.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/cube.frag)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/grid.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/grid.frag)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/particle.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/particle.frag)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/particle.comp)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/rml_ui.vert)
engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/rml_ui.frag)
