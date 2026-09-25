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
)
target_link_libraries(kke_tests PRIVATE kke_engine GTest::gtest_main)
target_compile_definitions(kke_tests PRIVATE KKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
gtest_discover_tests(kke_tests)
