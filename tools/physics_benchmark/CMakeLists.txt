add_executable(kke_physics_benchmark main.cpp)
target_link_libraries(kke_physics_benchmark PRIVATE femfx)
set_target_properties(kke_physics_benchmark PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
target_compile_options(kke_physics_benchmark PRIVATE -mavx2 -mfma)
target_compile_definitions(kke_physics_benchmark PRIVATE WIN32 NOMINMAX __forceinline=inline)
