# The starter game (docs/tutorials/getting-started.md). tools/new_game copies
# this folder and changes GAME_NAME; nothing else here needs editing to
# start.
set(GAME_NAME starter_game)

kke_add_game(${GAME_NAME} main.cpp PlayerModule.cpp PlayerBody.cpp)
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
# The game's own models and scenes (models/ and scenes/, any subfolders):
# models.load and scene.load find them by name, and the F4 scene editor
# saves into scenes/ here while you develop. Copied next to the executable
# like the scripts.
file(GLOB_RECURSE GAME_DATA_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/models/* ${CMAKE_CURRENT_SOURCE_DIR}/scenes/*)
foreach(f ${GAME_DATA_FILES})
    file(RELATIVE_PATH rel ${CMAKE_CURRENT_SOURCE_DIR} ${f})
    engine_copy_runtime_file(${GAME_NAME} ${f} games/${GAME_NAME}/${rel})
endforeach()
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSans-Italic.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(${GAME_NAME} ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader cube.vert cube.frag glow.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag
               rml_ui.vert rml_ui.frag rml_gradient.frag)
    engine_add_shader(${GAME_NAME} ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
