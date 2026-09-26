# The starter game (docs/tutorials/getting-started.md). tools/new_game copies
# this folder and changes GAME_NAME; nothing else here needs editing to
# start.
set(GAME_NAME starter_game)

add_executable(${GAME_NAME} main.cpp PlayerModule.cpp)
target_link_libraries(${GAME_NAME} PRIVATE kke_engine)
# Scripts run straight from this folder while you develop (save a file and
# the running game reloads it); the copy next to the executable is used
# when this folder isn't there (a game you've shipped).
target_compile_definitions(${GAME_NAME} PRIVATE
    GAME_SCRIPTS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    GAME_SCRIPTS_INSTALLED="games/${GAME_NAME}/scripts")

engine_copy_runtime_file(${GAME_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/${GAME_NAME}/game.json)
file(GLOB GAME_SCRIPT_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/*)
foreach(f ${GAME_SCRIPT_FILES})
    get_filename_component(fname ${f} NAME)
    engine_copy_runtime_file(${GAME_NAME} ${f} games/${GAME_NAME}/scripts/${fname})
endforeach()
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(${GAME_NAME} ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader cube.vert cube.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag
               rml_ui.vert rml_ui.frag rml_gradient.frag)
    engine_add_shader(${GAME_NAME} ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
