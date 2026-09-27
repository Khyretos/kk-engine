# The cookbook game (docs/cookbook/): every camera type, animation and IK
# recipes on the UAL mannequin, and the Lua side of both. The docs include
# regions of these files, and CI builds and runs it.
set(GAME_NAME cookbook)

kke_add_game(${GAME_NAME} main.cpp CookbookPlayer.cpp Mannequin.cpp)
target_link_libraries(${GAME_NAME} PRIVATE kke_engine)
target_compile_definitions(${GAME_NAME} PRIVATE
    GAME_SCRIPTS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    GAME_SCRIPTS_INSTALLED="games/${GAME_NAME}/scripts")

file(GLOB GAME_SCRIPT_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/*)
foreach(f ${GAME_SCRIPT_FILES})
    get_filename_component(fname ${f} NAME)
    engine_copy_runtime_file(${GAME_NAME} ${f} games/${GAME_NAME}/scripts/${fname})
endforeach()
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSans-Italic.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(${GAME_NAME} ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader cube.vert cube.frag glow.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag
               rml_ui.vert rml_ui.frag rml_gradient.frag)
    engine_add_shader(${GAME_NAME} ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The mannequin (Quaternius' Universal Animation Library, CC0) is found at
# runtime in assets/animations/ (or KKE_ANIMATIONS_DIR).
