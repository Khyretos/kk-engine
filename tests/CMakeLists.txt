# Unit tests for this engine's pure-logic code — see README "Test suite &
# coverage" for why Vulkan/GPU-touching code is deliberately NOT covered
# here, and stays verified by actually building and running the engine
# headlessly instead.
add_executable(kke_tests
    test_rml_text_safety.cpp
    test_game_manifest.cpp
    test_marketplace_index.cpp
    test_engine_error.cpp
)
target_link_libraries(kke_tests PRIVATE kke_engine GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(kke_tests)
