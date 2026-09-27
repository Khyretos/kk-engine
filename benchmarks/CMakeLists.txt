# The headless benchmark suite (docs/BENCHMARKS.md). kke_bench links the
# engine library only for its pure-logic code; it never opens a window.
add_executable(kke_bench kke_bench/main.cpp)
target_link_libraries(kke_bench PRIVATE kke_engine)

# The benchmark anyone can run (docs/BENCHMARKS.md "Benchmark for
# everyone"): plays every demo next to it and writes one results file.
# It lives in its own folder, bin/benchmark/, with the suite next to it as
# benchmark_suite.yaml, and finds the demos one folder up.
add_executable(kke_benchmark kke_benchmark/main.cpp)
target_link_libraries(kke_benchmark PRIVATE kke_engine)
set_target_properties(kke_benchmark PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/benchmark)
add_custom_command(TARGET kke_benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/suite.yaml $<TARGET_FILE_DIR:kke_benchmark>/benchmark_suite.yaml
    VERBATIM)
# Windows looks for DLLs next to the program: give it its own copies.
if(WIN32)
    add_custom_command(TARGET kke_benchmark POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:kke_benchmark> $<TARGET_FILE_DIR:kke_benchmark>
        COMMAND_EXPAND_LISTS VERBATIM)
endif()
