# Unit tests for this engine's pure-logic code — see README "Test suite &
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
    test_picking.cpp
    test_voxel_fracture.cpp
    test_particle_fluid.cpp
    test_melt_volume.cpp
    test_voronoi_fracture.cpp
    test_break_graph.cpp
    test_animator.cpp
    test_anim_rig.cpp
    test_resource_governor.cpp
    test_frame_stats.cpp
    test_camera_rig.cpp
    test_scene_file.cpp
    test_input_map.cpp
    test_input_devices.cpp
    test_floating_bodies.cpp
    test_audio.cpp
)
target_link_libraries(kke_tests PRIVATE kke_engine GTest::gtest_main)
if(KKE_ENABLE_JOLT)
    target_sources(kke_tests PRIVATE test_rigid_world.cpp test_locomotion.cpp)
endif()
if(ENGINE_ENABLE_LUA)
    target_sources(kke_tests PRIVATE test_script_vm.cpp)
endif()
target_compile_definitions(kke_tests PRIVATE KKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
# PRE_TEST: tests are listed when ctest runs, not at build time, so a
# cross build (Windows .exe on Linux) doesn't have to run them to link.
gtest_discover_tests(kke_tests DISCOVERY_MODE PRE_TEST)
