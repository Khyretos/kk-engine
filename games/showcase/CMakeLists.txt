# kke_demo: the walkable showcase (ACTION_PLAN.md P1).
add_executable(kke_demo main.cpp ShowcaseModule.cpp StressTest.cpp SplitScreen.cpp)
target_link_libraries(kke_demo PRIVATE kke_engine)
engine_copy_runtime_file(kke_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/showcase/game.json)
# Lua scripts (ScriptModule). The copy in bin/scripts/ is what runs; set
# KKE_SCRIPTS_DIR=<repo>/games/showcase/scripts to edit the originals live.
if(ENGINE_ENABLE_LUA)
    engine_copy_runtime_file(kke_demo ${CMAKE_CURRENT_SOURCE_DIR}/scripts/toys.lua scripts/toys.lua)
endif()
foreach(shader model.vert model_instanced.vert shadow_instanced.vert model.frag shadow.vert shadow.frag cube.vert cube.frag glow.frag)
    engine_add_shader(kke_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# Character animations (Quaternius' Universal Animation Library, CC0) are
# found at runtime in assets/animations/ (see ShowcaseModule::setupPlayer).
