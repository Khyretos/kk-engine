# Headless FEMFX experiments (fracture stability, worst-case load). Links
# kke_engine only for the plain-data geometry code (VoxelTets,
# VoronoiFracture); nothing here opens a window or touches Vulkan.
add_executable(kke_physics_lab main.cpp)
target_link_libraries(kke_physics_lab PRIVATE kke_engine femfx)
set_target_properties(kke_physics_lab PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(kke_physics_lab PRIVATE ${KKE_AVX2_FLAGS})
target_compile_definitions(kke_physics_lab PRIVATE WIN32 NOMINMAX $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:__forceinline=inline>)
