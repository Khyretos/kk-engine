# The headless benchmark suite (docs/BENCHMARKS.md). kke_bench links the
# engine library only for its pure-logic code; it never opens a window.
add_executable(kke_bench kke_bench/main.cpp)
target_link_libraries(kke_bench PRIVATE kke_engine)
