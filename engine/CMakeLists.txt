set(ENGINE_SOURCES
    src/vma_impl.cpp
    src/Window.cpp
    src/VulkanDevice.cpp
    src/SwapChain.cpp
    src/Pipeline.cpp
    src/Buffer.cpp
    src/Mesh.cpp
    src/Renderer.cpp
    src/DebugUi.cpp
    src/ShaderUtils.cpp
    src/Application.cpp
    src/RmlVulkanRenderInterface.cpp
    src/GameManifest.cpp
    src/EngineSettings.cpp
    src/ModelAsset.cpp
    src/Ragdoll.cpp
    src/BenchmarkReport.cpp
    src/AssetCatalog.cpp
    src/Picking.cpp
    src/VoxelTets.cpp
    src/InteriorColor.cpp
    src/FracturePattern.cpp
    src/VoronoiFracture.cpp
    src/BreakGraph.cpp
    src/Animator.cpp
    src/AnimRig.cpp
    src/ResourceGovernor.cpp
    src/FrameStats.cpp
    src/CameraRig.cpp
    src/SceneFile.cpp
    src/InputMap.cpp
    src/InputDevices.cpp
    src/modules/InputModule.cpp
    src/AudioMixer.cpp
    src/ImpactSynth.cpp
    src/modules/AudioModule.cpp
    src/modules/SoundVisualizerModule.cpp
    src/ParticleFluid.cpp
    src/MeltVolume.cpp
    src/SphereImpostors.cpp
    src/Ocean.cpp
    src/FloatingBodies.cpp
    src/OceanRenderer.cpp
    src/FluidSurface.cpp
    src/SystemInfo.cpp
    src/TetMeshAsset.cpp
    src/LightingBuffer.cpp
    src/ShadowMap.cpp
    src/Texture.cpp
    src/MarketplaceIndex.cpp
    src/RmlTextSafety.cpp
    src/Log.cpp
    src/HardwareCheck.cpp
    src/modules/GridModule.cpp
    src/modules/StatsModule.cpp
    src/modules/ParticleModule.cpp
    src/modules/OrbitCameraModule.cpp
    src/modules/UiModule.cpp
    src/modules/MarketplaceUiModule.cpp
    src/modules/LightingControlsModule.cpp
    src/modules/MaterialGridModule.cpp
    src/modules/DebugControlModule.cpp
    src/modules/PhysicsModule.cpp
    src/modules/SettingsModule.cpp
    src/modules/ModelModule.cpp
    src/modules/DebugDrawModule.cpp
)

set(ENGINE_HEADERS
    include/kke/Window.h
    include/kke/VulkanDevice.h
    include/kke/SwapChain.h
    include/kke/Pipeline.h
    include/kke/Buffer.h
    include/kke/Mesh.h
    include/kke/Renderer.h
    include/kke/VulkanCheck.h
    include/kke/DebugUi.h
    include/kke/ShaderUtils.h
    include/kke/Module.h
    include/kke/EngineError.h
    include/kke/Capabilities.h
    include/kke/Application.h
    include/kke/RmlVulkanRenderInterface.h
    include/kke/GameManifest.h
    include/kke/EngineSettings.h
    include/kke/ModelAsset.h
    include/kke/Ragdoll.h
    include/kke/BenchmarkReport.h
    include/kke/AssetCatalog.h
    include/kke/Picking.h
    include/kke/VoxelTets.h
    include/kke/InteriorColor.h
    include/kke/FracturePattern.h
    include/kke/ParticleFluid.h
    include/kke/MeltVolume.h
    include/kke/SphereImpostors.h
    include/kke/Ocean.h
    include/kke/FloatingBodies.h
    include/kke/OceanRenderer.h
    include/kke/FluidSurface.h
    include/kke/TetMeshAsset.h
    include/kke/LightingBuffer.h
    include/kke/ShadowMap.h
    include/kke/Texture.h
    include/kke/MarketplaceIndex.h
    include/kke/RmlTextSafety.h
    include/kke/Log.h
    include/kke/HardwareCheck.h
    include/kke/modules/GridModule.h
    include/kke/modules/StatsModule.h
    include/kke/modules/ParticleModule.h
    include/kke/modules/OrbitCameraModule.h
    include/kke/modules/UiModule.h
    include/kke/modules/SettingsModule.h
    include/kke/modules/ModelModule.h
    include/kke/modules/DebugDrawModule.h
    include/kke/modules/MarketplaceUiModule.h
    include/kke/modules/LightingControlsModule.h
    include/kke/modules/MaterialGridModule.h
    include/kke/modules/DebugControlModule.h
    include/kke/modules/PhysicsModule.h
    include/kke/Material.h
)

add_library(kke_engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(kke_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kke_engine PUBLIC
    volk
    GPUOpen::VulkanMemoryAllocator
    SDL3::SDL3
    glm::glm
    stb
    imgui
    RmlUi::Core
    nlohmann_json::nlohmann_json
    ufbx
    spdlog::spdlog
    kke_miniaudio
)

target_compile_definitions(kke_engine PUBLIC
    VK_NO_PROTOTYPES              # volk owns all Vulkan function pointers
    GLFW_INCLUDE_NONE
)

if(KKE_ENABLE_VALIDATION)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_VALIDATION=1)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_VALIDATION=0)
endif()

if(KKE_ENABLE_JOLT)
    target_sources(kke_engine PRIVATE src/RigidWorld.cpp src/Locomotion.cpp src/SceneLoader.cpp src/modules/RigidBodyModule.cpp)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_JOLT=1)
    target_link_libraries(kke_engine PUBLIC Jolt)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_JOLT=0)
endif()

if(ENGINE_ENABLE_LUA)
    target_sources(kke_engine PRIVATE src/ScriptVM.cpp src/modules/ScriptModule.cpp)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_LUA=1)
    target_link_libraries(kke_engine PUBLIC lua_lib)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_LUA=0)
endif()

if(KKE_ENABLE_GPU_PROFILER)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_GPU_PROFILER=1)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_GPU_PROFILER=0)
endif()

if(KKE_ENABLE_FEMFX)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_FEMFX=1)
    target_link_libraries(kke_engine PUBLIC femfx)
    # MUST match external/FEMFX/CMakeLists.txt's own compile options
    # exactly. FEMFX's headers are full of `inline` functions using
    # AVX2/FMA SIMD intrinsics directly (FmInitVector3, FmInitMatrix3,
    # etc.) — any translation unit that includes AMD_FEMFX.h and calls
    # them, like PhysicsModule.cpp, inlines and compiles that SIMD code
    # itself. Compiling it with different -m flags than the femfx
    # library itself was built with is a real, reproduced ABI mismatch
    # across the vector-register calling convention — found as an
    # actual segfault (that a stack-lifetime fix did NOT resolve,
    # ruling that theory out first) before landing on this as the
    # actual cause, not assumed.
    target_compile_options(kke_engine PUBLIC ${KKE_AVX2_FLAGS})
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_FEMFX=0)
endif()

if(WIN32)
    target_compile_definitions(kke_engine PUBLIC KKE_PLATFORM_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(kke_engine PUBLIC KKE_PLATFORM_MACOS=1)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_PLATFORM_LINUX=1)
endif()

# Recorded in benchmark reports (kke/BenchmarkReport.h) so results from
# Debug and Release builds are never confused with each other.
target_compile_definitions(kke_engine PRIVATE KKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
if(WIN32)
    target_link_libraries(kke_engine PUBLIC psapi) # peak memory in benchmark reports
endif()

# Hot CPU loops stay optimized in Debug builds too (same reasoning as
# FEMFX, BUGS.md BUG-029): texture mip generation, voxelization and
# fracture setup are pure number crunching, 5-20x slower at -O0, and
# nobody steps through them in a debugger. OPTIMIZATION.md log #15.
if(NOT MSVC)
    set_source_files_properties(src/Texture.cpp src/VoxelTets.cpp src/InteriorColor.cpp src/FracturePattern.cpp src/VoronoiFracture.cpp src/ParticleFluid.cpp src/MeltVolume.cpp src/Ocean.cpp src/FloatingBodies.cpp
        PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>")
endif()
