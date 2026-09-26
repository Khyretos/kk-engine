# kke_demo: the walkable showcase (ACTION_PLAN.md P1).
add_executable(kke_demo main.cpp ShowcaseModule.cpp StressTest.cpp SplitScreen.cpp Stations.cpp)
target_link_libraries(kke_demo PRIVATE kke_engine)
engine_copy_runtime_file(kke_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/showcase/game.json)
# Lua scripts (ScriptModule). The copy in bin/scripts/ is what runs; set
# KKE_SCRIPTS_DIR=<repo>/games/showcase/scripts to edit the originals live.
if(ENGINE_ENABLE_LUA)
    engine_copy_runtime_file(kke_demo ${CMAKE_CURRENT_SOURCE_DIR}/scripts/toys.lua scripts/toys.lua)
    # "Your first game in Lua" (games/first_lua_game): break-the-targets, T to play.
    foreach(f targets.lua targets_hud.rml)
        engine_copy_runtime_file(kke_demo ${CMAKE_SOURCE_DIR}/games/first_lua_game/scripts/${f} scripts/${f})
    endforeach()
endif()
# RmlUi (UiModule): script documents (ui.* in Lua) and the HUD.
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(kke_demo ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader rml_ui.vert rml_ui.frag rml_gradient.frag)
    engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
foreach(shader model.vert model_instanced.vert shadow_instanced.vert model.frag shadow.vert shadow.frag cube.vert cube.frag glow.frag ocean.vert ocean.frag sky.vert sky.frag)
    engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# Character animations (Quaternius' Universal Animation Library, CC0) are
# found at runtime in assets/animations/ (see ShowcaseModule::setupPlayer).
