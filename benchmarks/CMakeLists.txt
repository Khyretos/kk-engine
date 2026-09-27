# The headless benchmark suite (docs/BENCHMARKS.md). kke_bench links the
# engine library only for its pure-logic code; it never opens a window.
add_executable(kke_bench kke_bench/main.cpp)
target_link_libraries(kke_bench PRIVATE kke_engine)

# The benchmark anyone can run (docs/BENCHMARKS.md "Benchmark for
# everyone"): plays every demo next to it and writes one results file.
# The suite ships next to it as benchmark_suite.yaml.
add_executable(kke_benchmark kke_benchmark/main.cpp)
target_link_libraries(kke_benchmark PRIVATE kke_engine)
add_custom_command(TARGET kke_benchmark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/suite.yaml $<TARGET_FILE_DIR:kke_benchmark>/benchmark_suite.yaml
    VERBATIM)
