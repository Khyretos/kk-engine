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
    src/LogoMesh.cpp
    src/LogoIntro.cpp
    src/RmlVulkanRenderInterface.cpp
    src/GameManifest.cpp
    src/EngineSettings.cpp
    src/ModelAsset.cpp
    src/Ragdoll.cpp
    src/BenchmarkReport.cpp
    src/BenchRecorder.cpp
    src/AssetCatalog.cpp
    src/Picking.cpp
    src/DrawOrder.cpp
    src/PlayBlocks.cpp
    src/PlayScript.cpp
    src/Orders.cpp
    src/OrderScript.cpp
    src/OrderBridge.cpp
    src/NodeGraph.cpp
    src/TouchGestures.cpp
    src/VoxelTets.cpp
    src/InteriorColor.cpp
    src/FracturePattern.cpp
    src/VoronoiFracture.cpp
    src/BreakGraph.cpp
    src/Animator.cpp
    src/AnimRig.cpp
    src/ProceduralAnim.cpp
    src/ClimbWall.cpp
    src/Climber.cpp
    src/Combat.cpp
    src/Sidekick.cpp
    src/MeshLod.cpp
    src/ResourceGovernor.cpp
    src/HardwareTarget.cpp
    src/FrameStats.cpp
    src/Viewports.cpp
    src/PhysicsBridge.cpp
    src/PhysicsWorld.cpp
    src/Buoyancy.cpp
    src/CameraRig.cpp
    src/SceneFile.cpp
    src/ai/Steering.cpp
    src/ai/Perception.cpp
    src/ai/Utility.cpp
    src/ai/NavMesh.cpp
    src/ai/Species.cpp
    src/ai/AiWorld.cpp
    src/ai/AiScript.cpp
    src/ai/Clips.cpp
    src/ai/Learning.cpp
    src/net/BitStream.cpp
    src/net/Protocol.cpp
    src/net/Transport.cpp
    src/net/NetSession.cpp
    src/net/InputReplay.cpp
    src/net/ScriptSpawns.cpp
    src/net/SecureTransport.cpp
    src/net/Relay.cpp
    src/server/ServerConfig.cpp
    src/server/ServerFiles.cpp
    src/server/ServerAccess.cpp
    src/server/Leaderboard.cpp
    src/server/Directory.cpp
    src/voice/JitterBuffer.cpp
    src/storage/Store.cpp
    src/storage/MemoryStore.cpp
    src/storage/SqliteStore.cpp
    src/net/Authority.cpp
    src/net/Visibility.cpp
    src/GameRules.cpp
    src/License.cpp
    src/InputSanity.cpp
    src/PackSeal.cpp
    src/ContentPacks.cpp
    src/DataFile.cpp
    src/DevTools.cpp
    src/InputMap.cpp
    src/ButtonPrompts.cpp
    src/InputDevices.cpp
    src/modules/InputModule.cpp
    src/Lobby.cpp
    src/AudioMixer.cpp
    src/ImpactSynth.cpp
    src/Footsteps.cpp
    src/RoomAcoustics.cpp
    src/WavFile.cpp
    src/modules/AudioModule.cpp
    src/modules/SoundVisualizerModule.cpp
    src/ParticleFluid.cpp
    src/MeltVolume.cpp
    src/JigglePhysics.cpp
    src/SphereImpostors.cpp
    src/Ocean.cpp
    src/FloatingBodies.cpp
    src/OceanRenderer.cpp
    src/FluidSurface.cpp
    src/SystemInfo.cpp
    src/TetMeshAsset.cpp
    src/LightingBuffer.cpp
    src/Sky.cpp
    src/SkyRenderer.cpp
    src/Mood.cpp
    src/ShadowMap.cpp
    src/Texture.cpp
    src/TextureMips.cpp
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
    src/modules/LobbyModule.cpp
    src/modules/MaterialGridModule.cpp
    src/modules/DebugControlModule.cpp
    src/modules/PhysicsModule.cpp
    src/modules/PhysicsModuleBridge.cpp
    src/modules/PhysicsModuleRubble.cpp
    src/modules/SettingsModule.cpp
    src/modules/ModelModule.cpp
    src/modules/ThumbnailModule.cpp
    src/Thumbnails.cpp
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
    include/kke/LogoMesh.h
    include/kke/LogoIntro.h
    include/kke/EngineError.h
    include/kke/Capabilities.h
    include/kke/Application.h
    include/kke/RmlVulkanRenderInterface.h
    include/kke/GameManifest.h
    include/kke/EngineSettings.h
    include/kke/ModelAsset.h
    include/kke/Ragdoll.h
    include/kke/BenchmarkReport.h
    include/kke/BenchRecorder.h
    include/kke/AssetCatalog.h
    include/kke/Picking.h
    include/kke/PlayBlocks.h
    include/kke/Orders.h
    include/kke/OrderScript.h
    include/kke/OrderBridge.h
    include/kke/TouchGestures.h
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
    include/kke/Footsteps.h
    include/kke/RoomAcoustics.h
    include/kke/Thumbnails.h
    include/kke/Platform.h
    include/kke/HardwareTarget.h
    include/kke/modules/ThumbnailModule.h
    include/kke/TetMeshAsset.h
    include/kke/LightingBuffer.h
    include/kke/ShadowMap.h
    include/kke/Texture.h
    include/kke/MarketplaceIndex.h
    include/kke/ContentPacks.h
    include/kke/DataFile.h
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

# The OS services SDL3 doesn't cover (kke/Platform.h, docs/PLATFORMS.md)
# come from one backend folder. The public one covers desktop and mobile;
# a licensed console developer points this at their private backend.
set(KKE_PLATFORM_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/desktop" CACHE PATH
    "Folder with the kke::platform backend (a backend.cmake plus its sources). See docs/PLATFORMS.md.")
if(NOT EXISTS "${KKE_PLATFORM_BACKEND_DIR}/backend.cmake")
    message(FATAL_ERROR "KKE_PLATFORM_BACKEND_DIR='${KKE_PLATFORM_BACKEND_DIR}' has no backend.cmake (see docs/PLATFORMS.md)")
endif()
include("${KKE_PLATFORM_BACKEND_DIR}/backend.cmake")
if(NOT KKE_PLATFORM_BACKEND_SOURCES)
    message(FATAL_ERROR "${KKE_PLATFORM_BACKEND_DIR}/backend.cmake did not set KKE_PLATFORM_BACKEND_SOURCES")
endif()

add_library(kke_engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS} ${KKE_PLATFORM_BACKEND_SOURCES})

# The Kreative Kompas logo as the window/taskbar icon: the PNG is embedded
# into the engine at configure time (re-run automatically when it changes),
# so every game gets it without shipping or locating a file.
set(KKE_ICON_PNG ${CMAKE_SOURCE_DIR}/assets/branding/logo-128.png)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${KKE_ICON_PNG})
file(READ ${KKE_ICON_PNG} KKE_ICON_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," KKE_ICON_BYTES "${KKE_ICON_HEX}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated/EngineIcon.cpp
    "// Generated from assets/branding/logo-128.png by engine/CMakeLists.txt.\n"
    "#include <cstddef>\n"
    "namespace kke { extern const unsigned char kEngineIconPng[] = { ${KKE_ICON_BYTES} };\n"
    "extern const std::size_t kEngineIconPngSize = sizeof(kEngineIconPng); }\n")
target_sources(kke_engine PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated/EngineIcon.cpp)
# On Windows the .exe files carry the icon too (INTERFACE: compiled into
# each game that links the engine).
if(WIN32)
    enable_language(RC)
    target_sources(kke_engine INTERFACE ${CMAKE_SOURCE_DIR}/assets/branding/kk-engine.rc)
endif()

# The startup intro (kke::LogoIntro) and the sky (kke::SkyRenderer) can run
# in every game, so their shaders and the title font ship with the engine
# rather than with each game.
foreach(shader logo.vert logo.frag logo_backdrop.vert logo_backdrop.frag logo_text.vert logo_text.frag sky.vert sky.frag)
    engine_add_shader(kke_engine ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
engine_copy_runtime_file(kke_engine ${CMAKE_SOURCE_DIR}/assets/fonts/NotoSans-Bold.ttf assets/fonts/NotoSans-Bold.ttf)
kke_add_moods(kke_engine) # assets/moods and the CC0 skies (cmake/skies.cmake)

# Button prompt glyphs (kke/ButtonPrompts.h, Xelu's CC0 pack): the sets the
# engine shows go next to every executable, as assets/prompts/xelu/<set>/.
# The rest of the pack (older consoles, VR) stays in the repo for games
# that want it.
set(KKE_PROMPT_SETS keyboard_dark keyboard_light xbox_series ps5 switch steam_deck others/steam others/gestures)
set(_kke_prompt_files ${CMAKE_SOURCE_DIR}/assets/prompts/xelu/LICENSE.txt)
set(_kke_prompt_copy)
foreach(set IN LISTS KKE_PROMPT_SETS)
    file(GLOB _pngs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/prompts/xelu/${set}/*.png)
    list(APPEND _kke_prompt_files ${_pngs})
    list(APPEND _kke_prompt_copy COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/prompts/xelu/${set} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/prompts/xelu/${set})
endforeach()
set(_kke_prompt_stamp ${CMAKE_CURRENT_BINARY_DIR}/prompt_glyphs.stamp)
add_custom_command(
    OUTPUT ${_kke_prompt_stamp}
    ${_kke_prompt_copy}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/assets/prompts/xelu/LICENSE.txt
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/prompts/xelu/LICENSE.txt
    COMMAND ${CMAKE_COMMAND} -E touch ${_kke_prompt_stamp}
    DEPENDS ${_kke_prompt_files}
    COMMENT "Copying button prompt glyphs"
)
add_custom_target(kke_prompt_glyphs DEPENDS ${_kke_prompt_stamp})
add_dependencies(kke_engine kke_prompt_glyphs)

target_include_directories(kke_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(kke_engine PRIVATE yaml-cpp::yaml-cpp) # kke/DataFile.h only
target_link_libraries(kke_engine PRIVATE meshoptimizer) # kke/MeshLod.h only

target_link_libraries(kke_engine PUBLIC
    volk
    GPUOpen::VulkanMemoryAllocator
    SDL3::SDL3
    glm::glm
    stb
    imgui
    RmlUi::Core
    nlohmann_json::nlohmann_json
    kke_sqlite
    kke_recast
    kke_genann
    ufbx
    spdlog::spdlog
    kke_miniaudio
    kke_monocypher
)

target_compile_definitions(kke_engine PUBLIC
    VK_NO_PROTOTYPES              # volk owns all Vulkan function pointers
    GLFW_INCLUDE_NONE
)

# Shipping builds compile the developer tools out (kke/DevTools.h).
if(KKE_SHIPPING)
    target_compile_definitions(kke_engine PUBLIC KKE_DEV_TOOLS=0)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_DEV_TOOLS=1)
endif()

if(KKE_ENABLE_VALIDATION)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_VALIDATION=1)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_VALIDATION=0)
endif()

if(KKE_ENABLE_JOLT)
    target_sources(kke_engine PRIVATE src/RigidWorld.cpp src/Locomotion.cpp src/SceneLoader.cpp src/modules/RigidBodyModule.cpp src/modules/PhysicsBridgeModule.cpp
        src/net/WorldMoveCheck.cpp src/net/LevelSight.cpp src/net/LocomotionReplay.cpp)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_JOLT=1)
    target_link_libraries(kke_engine PUBLIC Jolt)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_JOLT=0)
endif()

if(KKE_ENABLE_NET)
    target_sources(kke_engine PRIVATE src/net/EnetTransport.cpp src/server/DirectoryNet.cpp src/server/DedicatedServer.cpp
        src/server/RelayService.cpp)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_NET=1)
    target_link_libraries(kke_engine PUBLIC kke_enet)
    if(KKE_ENABLE_JOLT)
        target_sources(kke_engine PRIVATE src/modules/NetModule.cpp)
    endif()
    if(KKE_ENABLE_VOICE)
        target_sources(kke_engine PRIVATE src/voice/VoiceCodec.cpp src/voice/VoiceCleaner.cpp)
        target_link_libraries(kke_engine PUBLIC opus PRIVATE kke_rnnoise kke_speexdsp)
        target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_VOICE=1)
        if(KKE_ENABLE_JOLT)
            target_sources(kke_engine PRIVATE src/modules/VoiceModule.cpp)
        endif()
    else()
        target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_VOICE=0)
    endif()
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_NET=0)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_VOICE=0)
endif()

if(KKE_ENABLE_VALKEY)
    target_sources(kke_engine PRIVATE src/storage/ValkeyStore.cpp)
    target_link_libraries(kke_engine PUBLIC kke_hiredis)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_VALKEY=1)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_VALKEY=0)
endif()
if(KKE_ENABLE_POSTGRES)
    target_sources(kke_engine PRIVATE src/storage/PostgresStore.cpp)
    target_link_libraries(kke_engine PUBLIC PostgreSQL::PostgreSQL)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_POSTGRES=1)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_POSTGRES=0)
endif()
if(KKE_ENABLE_STEAM_AUDIO)
    target_sources(kke_engine PRIVATE src/SteamAudioSpatializer.cpp)
    target_link_libraries(kke_engine PUBLIC kke_steamaudio)
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_STEAM_AUDIO=1)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_STEAM_AUDIO=0)
endif()

if(ENGINE_ENABLE_LUA)
    target_sources(kke_engine PRIVATE src/ScriptVM.cpp src/ScriptCalls.cpp src/ScriptStore.cpp src/modules/ScriptModule.cpp src/modules/ScriptBindings.cpp src/modules/ScriptReplication.cpp)
    if(KKE_ENABLE_NET)
        target_sources(kke_engine PRIVATE src/server/ServerScripts.cpp)
    endif()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_LUA=1)
    target_link_libraries(kke_engine PUBLIC lua_lib)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_ENABLE_LUA=0)
endif()

if(KKE_ENABLE_GPU_PROFILER)
    # The option compiles against VulkanProfiler's extension header, which
    # only a separate VulkanProfiler install provides (docs/BUILDING.md
    # section 4). Say so at configure time instead of failing every
    # Vulkan source file with "VkProfilerEXT.h: No such file".
    find_path(KKE_VK_PROFILER_INCLUDE_DIR VkProfilerEXT.h
        PATH_SUFFIXES include include/vulkan VkLayer_profiler_layer)
    if(NOT KKE_VK_PROFILER_INCLUDE_DIR)
        message(FATAL_ERROR "KKE_ENABLE_GPU_PROFILER is ON (the 'everything' presets) but VkProfilerEXT.h was not found. "
            "Install VulkanProfiler (docs/BUILDING.md, section 4), set KKE_VK_PROFILER_INCLUDE_DIR to its include folder, "
            "or configure with -DKKE_ENABLE_GPU_PROFILER=OFF.")
    endif()
    target_include_directories(kke_engine PUBLIC ${KKE_VK_PROFILER_INCLUDE_DIR})
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
elseif(ANDROID)
    target_compile_definitions(kke_engine PUBLIC KKE_PLATFORM_ANDROID=1)
elseif(APPLE)
    target_compile_definitions(kke_engine PUBLIC KKE_PLATFORM_MACOS=1)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_PLATFORM_LINUX=1)
endif()

# Recorded in benchmark reports (kke/BenchmarkReport.h) so results from
# Debug and Release builds are never confused with each other.
target_compile_definitions(kke_engine PRIVATE KKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
# ...and which code: the release name (the Release workflow passes the
# tag) and the commit this build tree was configured at.
set(KKE_VERSION_NAME "dev" CACHE STRING "Version name written into benchmark reports (the Release workflow sets the tag)")
execute_process(COMMAND git rev-parse --short=10 HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE _kke_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _kke_commit_rc)
if(NOT _kke_commit_rc EQUAL 0 OR _kke_commit STREQUAL "")
    set(_kke_commit "unknown")
endif()
target_compile_definitions(kke_engine PRIVATE KKE_VERSION_NAME="${KKE_VERSION_NAME}" KKE_GIT_COMMIT="${_kke_commit}")

# The hardware target a build starts on when KKE_TARGET isn't set
# (kke/HardwareTarget.h). Empty = detect the device. The hardware presets
# in CMakePresets.json set it (steam-deck, handheld-pc, desktop-low).
set(KKE_DEFAULT_TARGET "" CACHE STRING "Hardware target this build defaults to (steam-deck, handheld-pc, desktop-low, android, ios, desktop); empty = detect. docs/PLATFORMS.md")
target_compile_definitions(kke_engine PRIVATE KKE_DEFAULT_TARGET="${KKE_DEFAULT_TARGET}")
if(KKE_PLATFORM_BACKEND_LIBRARIES)
    target_link_libraries(kke_engine PUBLIC ${KKE_PLATFORM_BACKEND_LIBRARIES})
endif()

# Hot CPU loops stay optimized in Debug builds too (same reasoning as
# FEMFX, BUGS.md BUG-029): texture mip generation, voxelization and
# fracture setup are pure number crunching, 5-20x slower at -O0, and
# nobody steps through them in a debugger. docs/OPTIMIZATION.md log #15.
if(NOT MSVC)
    set_source_files_properties(src/Sky.cpp src/Texture.cpp src/TextureMips.cpp src/VoxelTets.cpp src/InteriorColor.cpp src/FracturePattern.cpp src/VoronoiFracture.cpp src/ParticleFluid.cpp src/MeltVolume.cpp src/JigglePhysics.cpp src/Ocean.cpp src/FloatingBodies.cpp
        PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>")
endif()
