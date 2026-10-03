# Unit tests for this engine's pure-logic code — see docs/HISTORY.md "Test suite &
# coverage" for why Vulkan/GPU-touching code is deliberately NOT covered
# here, and stays verified by actually building and running the engine
# headlessly instead.
add_executable(kke_tests
    test_rml_text_safety.cpp
    test_game_manifest.cpp
    test_tet_mesh_asset.cpp
    test_marketplace_index.cpp
    test_engine_error.cpp
    test_engine_settings.cpp
    test_model_asset.cpp
    test_ragdoll.cpp
    test_benchmark_report.cpp
    test_asset_catalog.cpp
    test_known_packs.cpp
    test_picking.cpp
    test_play_blocks.cpp
    test_orders.cpp
    test_node_graph.cpp
    test_ai.cpp
    test_touch_gestures.cpp
    test_demo_panel.cpp
    test_voxel_fracture.cpp
    test_interior_fill.cpp
    test_particle_fluid.cpp
    test_melt_volume.cpp
    test_voronoi_fracture.cpp
    test_break_graph.cpp
    test_animator.cpp
    test_anim_rig.cpp
    test_character_ik.cpp
    test_body_shape.cpp
    test_procedural_anim.cpp
    test_climb_wall.cpp
    test_climb_mountains.cpp
    ${CMAKE_SOURCE_DIR}/games/climb_race/Mountains.cpp
    ${CMAKE_SOURCE_DIR}/games/climb_race/NetRace.cpp
    ${CMAKE_SOURCE_DIR}/games/climb_race/Progress.cpp
    ${CMAKE_SOURCE_DIR}/games/climb_race/Ghost.cpp
    test_tennis.cpp
    ${CMAKE_SOURCE_DIR}/games/tennis/Rules.cpp
    ${CMAKE_SOURCE_DIR}/games/tennis/Court.cpp
    ${CMAKE_SOURCE_DIR}/games/tennis/Shot.cpp
    ${CMAKE_SOURCE_DIR}/games/tennis/Swing.cpp
    ${CMAKE_SOURCE_DIR}/games/tennis/Bot.cpp
    ${CMAKE_SOURCE_DIR}/games/tennis/NetTennis.cpp
    test_flight.cpp
    ${CMAKE_SOURCE_DIR}/games/flying_demo/Flight.cpp
    ${CMAKE_SOURCE_DIR}/games/flying_demo/Course.cpp
    ${CMAKE_SOURCE_DIR}/games/flying_demo/FlyNet.cpp
    ${CMAKE_SOURCE_DIR}/games/flying_demo/Stunts.cpp
    ${CMAKE_SOURCE_DIR}/games/flying_demo/Combat.cpp
    test_combat.cpp
    test_sidekick.cpp
    test_mesh_lod.cpp
    test_resource_governor.cpp
    test_hardware_target.cpp
    test_frame_stats.cpp
    test_bench_recorder.cpp
    test_cooked_file.cpp
    test_viewports.cpp
    test_lobby.cpp
    test_game_shell.cpp
    test_physics_bridge.cpp
    test_physics_world.cpp
    test_buoyancy.cpp
    test_camera_rig.cpp
    test_scene_file.cpp
    test_net.cpp
    test_input_replay.cpp
    test_server.cpp
    test_relay.cpp
    test_synced_tables.cpp
    test_voice.cpp
    test_storage.cpp
    test_input_map.cpp
    test_button_prompts.cpp
    test_ui_profile.cpp
    test_input_devices.cpp
    test_floating_bodies.cpp
    test_audio.cpp
    test_engine_sound.cpp
    test_tyre.cpp
    test_footsteps.cpp
    test_room_acoustics.cpp
    test_wav_file.cpp
    test_spatializer.cpp
    test_thumbnails.cpp
    test_logo_mesh.cpp
    test_texture_mips.cpp
    test_shadow_projection.cpp
    test_draw_order.cpp
    test_tone_mapper.cpp
    test_sky.cpp
    test_jiggle.cpp
    test_rml_data_binding.cpp
    test_frame_retire_queue.cpp
    test_anti_cheat.cpp
    test_fair_play.cpp
    test_content_packs.cpp
    test_data_file.cpp
)
target_link_libraries(kke_tests PRIVATE kke_engine GTest::gtest_main)
if(KKE_ENABLE_JOLT)
    target_sources(kke_tests PRIVATE test_rigid_world.cpp test_cloth.cpp test_hair.cpp test_locomotion.cpp test_jolt_ragdoll.cpp test_move_check.cpp)
endif()
if(ENGINE_ENABLE_LUA)
    target_sources(kke_tests PRIVATE test_script_vm.cpp test_cookbook.cpp)
    if(KKE_ENABLE_NET)
        target_sources(kke_tests PRIVATE test_server_scripts.cpp)
    endif()
endif()
target_compile_definitions(kke_tests PRIVATE KKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# The party game's show, glass and wire format (test_party.cpp).
if(KKE_ENABLE_JOLT AND KKE_ENABLE_NET)
    target_sources(kke_tests PRIVATE test_party.cpp
        ${CMAKE_SOURCE_DIR}/games/party/Show.cpp
        ${CMAKE_SOURCE_DIR}/games/party/Shatter.cpp
        ${CMAKE_SOURCE_DIR}/games/party/NetParty.cpp)
    target_include_directories(kke_tests PRIVATE ${CMAKE_SOURCE_DIR}/games/party)
endif()

# ToneMapper.ValuesMatchTheShader reads shaders/tonemap.glsl. It is built
# into the test binary because the Release workflow runs kke_tests from a
# packaged build (no source tree, and the package ships only SPIR-V).
file(READ ${CMAKE_SOURCE_DIR}/shaders/tonemap.glsl KKE_TONEMAP_GLSL)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/tonemap.glsl)
configure_file(tonemap_glsl.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/tonemap_glsl.h @ONLY)
target_include_directories(kke_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

# The cookbook's Lua recipes (docs/cookbook/recipes/), built in the same
# way for test_cookbook.cpp: each must compile, and the play ones run.
file(GLOB_RECURSE KKE_COOKBOOK_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/docs/cookbook/recipes/*.lua)
list(SORT KKE_COOKBOOK_FILES)
set(KKE_COOKBOOK_RECIPES "")
foreach(f ${KKE_COOKBOOK_FILES})
    file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR}/docs/cookbook/recipes ${f})
    file(READ ${f} lua)
    string(APPEND KKE_COOKBOOK_RECIPES "    { \"${rel}\", R\"KKE_RECIPE(${lua})KKE_RECIPE\" },\n")
endforeach()
configure_file(cookbook_recipes.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/cookbook_recipes.h @ONLY)
target_include_directories(kke_tests PRIVATE ${CMAKE_SOURCE_DIR}/games/cookbook)
# Climb Race's mountain files and race setup (test_climb_mountains.cpp).
target_include_directories(kke_tests PRIVATE ${CMAKE_SOURCE_DIR}/games/climb_race)
# The Flying demo's flight model and island (test_flight.cpp).
target_include_directories(kke_tests PRIVATE ${CMAKE_SOURCE_DIR}/games/flying_demo)
target_compile_definitions(kke_tests PRIVATE KKE_CLIMB_MOUNTAINS_DIR="${CMAKE_SOURCE_DIR}/games/climb_race/mountains")
# Tennis's rules, flight maths and CPU footwork (test_tennis.cpp).
target_include_directories(kke_tests PRIVATE ${CMAKE_SOURCE_DIR}/games/tennis)

# The shipped moods (assets/moods/), built in the same way for test_sky.cpp.
file(GLOB KKE_MOOD_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/moods/*.yaml)
list(SORT KKE_MOOD_FILES)
set(KKE_MOOD_ENTRIES "")
foreach(f ${KKE_MOOD_FILES})
    get_filename_component(n ${f} NAME)
    file(READ ${f} text)
    string(APPEND KKE_MOOD_ENTRIES "    { \"${n}\", R\"KKE_MOOD(${text})KKE_MOOD\" },\n")
endforeach()
set(KKE_SKY_NAMES "")
foreach(entry ${KKE_SKIES})
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 name)
    string(APPEND KKE_SKY_NAMES "\"${name}\", ")
endforeach()
configure_file(moods_data.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/moods_data.h @ONLY)

include(GoogleTest)
# PRE_TEST: tests are listed when ctest runs, not at build time, so a
# cross build (Windows .exe on Linux) doesn't have to run them to link.
gtest_discover_tests(kke_tests DISCOVERY_MODE PRE_TEST)
