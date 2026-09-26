cmake_minimum_required(VERSION 3.24)
project(kreative_kompas_engine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Options — every "lego piece" can be toggled independently.
# ---------------------------------------------------------------------------
option(KKE_ENABLE_VALIDATION "Enable Vulkan validation layers in debug" ON)
option(ENGINE_ENABLE_LUA "Build with Lua scripting support" OFF) # wired later
option(ENGINE_HEADLESS_TEST "Build the headless smoke-test target" ON)
option(KKE_ENABLE_TESTS "Build the unit test suite (tests/)" ON)
option(KKE_ENABLE_JOLT "Build the Jolt Physics rigid-body/collision layer (MIT): static world collision, rigid bodies, character controller. See ACTION_PLAN.md 1.1." ON)
option(KKE_ENABLE_FEMFX "Build the vendored, patched AMD FEMFX deformable-physics library. See README 'Physics: AMD FEMFX integration'." OFF)
option(KKE_ENABLE_COVERAGE "Instrument builds for code coverage (gcc/clang --coverage). See README 'Test suite & coverage'." OFF)
option(KKE_ENABLE_GPU_PROFILER "Request the VulkanProfiler layer (VK_LAYER_PROFILER_unified) at instance creation if installed. See README 'GPU profiler (VulkanProfiler) integration'." OFF)

if(KKE_ENABLE_COVERAGE)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "KKE_ENABLE_COVERAGE is most meaningful with -DCMAKE_BUILD_TYPE=Debug (no optimization skewing line counts).")
    endif()
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

include(FetchContent)

# FEMFX's SIMD code needs AVX2 + FMA, and everything that includes its
# headers must be compiled with the same flags (a mismatch was a real,
# reproduced ABI crash, see engine/CMakeLists.txt). Spelled per compiler.
if(MSVC)
    set(KKE_AVX2_FLAGS /arch:AVX2)
else()
    set(KKE_AVX2_FLAGS -mavx2 -mfma)
endif()

# ---------------------------------------------------------------------------
# Vulkan headers/loader (system SDK — validated via find_package)
# ---------------------------------------------------------------------------
# Only the *headers* are needed at build time: volk loads the Vulkan
# driver at runtime. When there's no Vulkan SDK for the target (cross-
# compiling for Windows from Linux, docker/windows.Dockerfile), the
# official headers are fetched instead.
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(STATUS "Vulkan SDK not found for this target: fetching Vulkan-Headers (volk loads the driver at runtime)")
    FetchContent_Declare(
        vulkan_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        v1.3.290
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(vulkan_headers)
    set(Vulkan_INCLUDE_DIR "${vulkan_headers_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(Vulkan_INCLUDE_DIRS "${vulkan_headers_SOURCE_DIR}/include")
    if(NOT TARGET Vulkan::Headers)
        add_library(Vulkan::Headers ALIAS Vulkan-Headers)
    endif()
endif()

# ---------------------------------------------------------------------------
# volk — Vulkan meta-loader. One job, does it well.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        1.3.270
    GIT_SHALLOW    TRUE
)
set(VOLK_STATIC_DEFINES "")
FetchContent_MakeAvailable(volk)

# ---------------------------------------------------------------------------
# Vulkan Memory Allocator — header-only, saves us from hand-rolled VkDeviceMemory pain.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(vma)

# ---------------------------------------------------------------------------
# SDL3 — windowing + input. Built from source so it's identical on every platform.
# ---------------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.10
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3)

# SDL3's own vendored source produces occasional warnings depending on
# which platform backends get compiled (X11, Wayland, KMSDRM, etc. all
# vary by system) — e.g. -Wdiscarded-qualifiers in SDL_string.c,
# -Wunused-variable in the Wayland mouse backend. Not our code to fix,
# and SDL3 is actively maintained upstream, not something this project
# has taken ownership of patching the way external/FEMFX has. Given
# warnings from different SDL3 files surface on different systems'
# platform-backend combinations, suppressing broadly for this one
# third-party target is more robust than chasing individual warning
# types file by file as each new system reveals a different one.
if(TARGET SDL3-static)
    target_compile_options(SDL3-static PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# GLM — header-only math library.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# ---------------------------------------------------------------------------
# stb — header-only utilities (stb_image now, more of the stb_* family later).
# ---------------------------------------------------------------------------
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Dear ImGui — debug UI. Core + SDL3/Vulkan backends only (no demo-app cruft).
# ---------------------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC volk SDL3::SDL3)
target_compile_definitions(imgui PUBLIC
    VK_NO_PROTOTYPES
    IMGUI_IMPL_VULKAN_NO_PROTOTYPES
)

# ---------------------------------------------------------------------------
# RmlUi — HTML/CSS-inspired UI library (game-facing UI, not a debug tool
# like ImGui). Requires a real font engine to initialise at all, so we use
# its bundled FreeType backend (system package, found via find_package)
# rather than a hand-written stub. See engine/include/kke/modules/UiModule.h
# for exactly what is and isn't wired up yet — this is a multi-slice
# integration, and rendering isn't live until a later slice.
# ---------------------------------------------------------------------------
# FreeType: the system's when there is one (Linux packages), otherwise
# built from source (cross builds for Windows/Android have none). The
# override makes RmlUi's own find_package(Freetype) pick up this build.
find_package(Freetype QUIET)
if(NOT Freetype_FOUND AND NOT FREETYPE_FOUND)
    message(STATUS "FreeType not found for this target: building it from source")
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG        VER-2-13-3
        GIT_SHALLOW    TRUE
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(freetype)
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    set(FREETYPE_FOUND TRUE)
    set(FREETYPE_LIBRARIES freetype)
    set(FREETYPE_INCLUDE_DIRS "${freetype_SOURCE_DIR}/include")
endif()
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG        6.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(rmlui)

# ---------------------------------------------------------------------------
# nlohmann/json — the engine's one JSON library. Foundational rather than
# optional: game.json manifests, the marketplace index, and (eventually)
# save data are all specified to be JSON — see GameManifest.h and the
# README's "Game folder convention & marketplace" section.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# spdlog — the engine's one logging library. Async by design (see
# engine/include/kke/Log.h): log calls push onto a queue consumed by a
# background thread, so a burst of log lines never blocks the render/game
# thread waiting on stdout I/O. Header+source, MIT licensed.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# ---------------------------------------------------------------------------
# ufbx — FBX (and OBJ) loading for art packs like Synty's: meshes,
# materials, skeletons, skin weights, animations. MIT licensed, a single
# .c/.h pair with no dependencies of its own (see kke/ModelAsset.h).
# Chosen over assimp for being small, fast to build, and very thorough on
# FBX specifically, which is the format most asset-store packs ship.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG        v0.23.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ufbx)
if(NOT TARGET ufbx)
    add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
    target_include_directories(ufbx PUBLIC ${ufbx_SOURCE_DIR})
    set_target_properties(ufbx PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Always optimized, same reasoning as FEMFX: nobody steps through the
    # FBX parser, and a Debug -O0 parse of a big pack is needlessly slow.
    target_compile_options(ufbx PRIVATE $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>)
endif()

# ---------------------------------------------------------------------------
# GoogleTest — the unit test framework for this engine's pure-logic code
# (manifest parsing, marketplace dedup, text-escaping — see README "Test
# suite & coverage"). Deliberately NOT used to unit-test Vulkan/GPU code;
# that stays verified the way it already has been, by actually building
# and running the engine headlessly against lavapipe.
# ---------------------------------------------------------------------------
if(KKE_ENABLE_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    # Static, whatever RmlUi's BUILD_SHARED_LIBS=ON cache option says:
    # a shared gtest/gmock with static libgcc fails to link on MinGW
    # (duplicate _Unwind_Resume), and tests don't need DLLs anyway.
    set(KKE_SAVED_SHARED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(googletest)
    set(BUILD_SHARED_LIBS ${KKE_SAVED_SHARED})
endif()

# ---------------------------------------------------------------------------
# Lua — scripting hooks. Off by default until the engine actually calls into it.
# ---------------------------------------------------------------------------
if(ENGINE_ENABLE_LUA)
    FetchContent_Declare(
        lua
        URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
    )
    FetchContent_MakeAvailable(lua)
    file(GLOB LUA_CORE_SOURCES ${lua_SOURCE_DIR}/src/*.c)
    list(FILTER LUA_CORE_SOURCES EXCLUDE REGEX ".*/(lua|luac)\\.c$")
    add_library(lua_lib STATIC ${LUA_CORE_SOURCES})
    target_include_directories(lua_lib PUBLIC ${lua_SOURCE_DIR}/src)
endif()

# ---------------------------------------------------------------------------
# Shader compilation (GLSL -> SPIR-V at build time via glslc/glslangValidator)
# ---------------------------------------------------------------------------
find_program(GLSLC_EXECUTABLE glslc)
find_program(GLSLANG_VALIDATOR_EXECUTABLE glslangValidator)

# Shared by every game's CMakeLists.txt (originally duplicated as a
# locally-named function per game — kke_copy_runtime_file,
# physics_demo_copy_runtime_file — until adding a second game that
# copies the *same* source file (the bundled fonts) to the *same*
# output path exposed a real conflict: Ninja refuses "multiple rules
# generate bin/assets/fonts/NotoSans-Regular.ttf" when two separately-
# named functions each declare their own add_custom_command for that
# identical output. One shared function, with the same "don't
# redeclare" guard as engine_add_shader below, fixes this the same way.
#
# OUTPUT-based custom commands with explicit DEPENDS, not POST_BUILD:
# a POST_BUILD command only runs when the target itself actually
# relinks, so editing a font/manifest file without touching any source
# file would silently NOT re-copy it on the next `cmake --build` —
# found exactly this bug while testing the hardware-requirements check
# (had to copy game.json by hand to verify a change took effect).
function(engine_copy_runtime_file TARGET SRC DEST_RELATIVE)
    set(output_path ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${DEST_RELATIVE})
    get_filename_component(output_dir ${output_path} DIRECTORY)
    string(MD5 target_suffix "${output_path}")
    set(copy_target_name "engine_copy_${target_suffix}")
    if(NOT TARGET ${copy_target_name})
        add_custom_command(
            OUTPUT ${output_path}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SRC} ${output_path}
            DEPENDS ${SRC}
            COMMENT "Copying ${DEST_RELATIVE}"
        )
        add_custom_target(${copy_target_name} DEPENDS ${output_path})
    endif()
    add_dependencies(${TARGET} ${copy_target_name})
endfunction()

# Shared GLSL included by several shaders (#include "pbr_common.glsl");
# every shader rebuilds when one changes.
file(GLOB KKE_SHADER_INCLUDES ${CMAKE_SOURCE_DIR}/shaders/*.glsl)

function(engine_add_shader TARGET SHADER_SOURCE)
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${SHADER_NAME}.spv)
    get_filename_component(SHADER_OUTPUT_DIR ${SHADER_OUTPUT} DIRECTORY)

    if(GLSLC_EXECUTABLE)
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE} ${KKE_SHADER_INCLUDES}
            COMMENT "Compiling shader ${SHADER_NAME} (glslc)"
        )
    elseif(GLSLANG_VALIDATOR_EXECUTABLE)
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
            COMMAND ${GLSLANG_VALIDATOR_EXECUTABLE} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE} ${KKE_SHADER_INCLUDES}
            COMMENT "Compiling shader ${SHADER_NAME} (glslangValidator)"
        )
    else()
        message(FATAL_ERROR "Neither glslc nor glslangValidator found — install the Vulkan SDK or glslang-tools.")
    endif()

    string(REPLACE "." "_" SHADER_TARGET_NAME "shader_${SHADER_NAME}")
    # Guarded: a shader file used by more than one executable (as of
    # games/physics_demo reusing kke_demo_game's cube.vert/frag for its
    # own render bridge) would otherwise try to declare this same
    # target twice — CMake target names are global, not per-consumer.
    # The underlying compiled .spv output path is already shared/keyed
    # by filename, so this only needs to exist once regardless of how
    # many executables depend on it.
    if(NOT TARGET ${SHADER_TARGET_NAME})
        add_custom_target(${SHADER_TARGET_NAME} DEPENDS ${SHADER_OUTPUT})
    endif()
    add_dependencies(${TARGET} ${SHADER_TARGET_NAME})
endfunction()

# ---------------------------------------------------------------------------
# Engine core library
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Jolt Physics — rigid bodies, collision, character controller (MIT).
# FEMFX stays for deformable/breakable hero objects; Jolt carries the rest
# (the world's collision, rocks, debris, players): SCALING.md.
# ---------------------------------------------------------------------------
if(KKE_ENABLE_JOLT)
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
    set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
    set(OBJECT_LAYER_BITS 16 CACHE STRING "" FORCE)
    # Jolt's own GPU compute backends (hair) need DXC/Metal toolchains;
    # the rigid-body core doesn't. Our renderer owns the GPU.
    set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
    set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
    set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
    # Same SSE4.2/AVX level everywhere Jolt is used; x86 builds only
    # (ARM builds use Jolt's NEON path automatically).
    FetchContent_Declare(
        JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG        v5.6.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  Build
    )
    set(KKE_SAVED_SHARED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(JoltPhysics)
    set(BUILD_SHARED_LIBS ${KKE_SAVED_SHARED})
    # Optimized even in Debug (same reasoning as FEMFX: a dense solver).
    if(NOT MSVC)
        target_compile_options(Jolt PRIVATE $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>)
    endif()
endif()

# FEMFX (if enabled) must be added before engine/, since engine's own
# CMakeLists.txt links against the femfx target — found by actually
# hitting this ordering issue rather than assuming it, see README
# "Physics: AMD FEMFX integration."
if(KKE_ENABLE_FEMFX)
    add_subdirectory(external/FEMFX)
endif()

add_subdirectory(engine)

# (The CGAL-based offline tetrahedralizer that used to live in tools/ was
# removed on 2026-09-26: kke::voxelizeToTets does the job at runtime on any
# mesh, including open ones CGAL rejects, and CGAL's GPL licence was a risk
# for an engine that powers a marketplace. See README "Content pipeline".)

# The physics multithreading benchmark — links femfx directly (not
# kke_engine), so it only needs KKE_ENABLE_FEMFX. See tools/physics_benchmark/
# main.cpp's own top comment for why this tool exists: this sandbox
# has exactly 1 CPU core, so no real speedup could ever be measured
# here — this tool is what lets that be measured honestly on whatever
# hardware actually has more than one.
if(KKE_ENABLE_FEMFX)
    add_subdirectory(tools/physics_benchmark)
    add_subdirectory(tools/physics_lab)
endif()

# ---------------------------------------------------------------------------
# App executable (the actual window + cube demo)
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Games — each subdirectory of games/ is a self-contained "game folder"
# (see README "Game folder convention & marketplace"). Only one is built
# by default right now; a real multi-game build (or marketplace-driven
# discovery) is tracked in the Roadmap, not implemented yet — this single
# add_subdirectory is today's honest state, not a placeholder pretending
# to be more general than it is.
# ---------------------------------------------------------------------------
add_subdirectory(games/kke_demo_game)

# A dedicated physics demo, not another module bolted onto
# kke_demo_game — see games/physics_demo/main.cpp's own comment for
# why: real physics units and kke_demo_game's small-scale content
# don't share a sensible camera. Only meaningful with FEMFX actually
# built; skipped entirely otherwise rather than building a binary that
# can only print an error and exit.
if(KKE_ENABLE_FEMFX)
    add_subdirectory(games/physics_demo)
endif()

# A dedicated demo for Dear ImGui — see games/imgui_demo/main.cpp for
# details. Always built (no gate) — has no dependency beyond the core
# engine, unlike physics_demo.
add_subdirectory(games/imgui_demo)

# A dedicated demo for RmlUi — see games/rmlui_demo/main.cpp for
# details. Always built, same reasoning as imgui_demo above.
add_subdirectory(games/rmlui_demo)
add_subdirectory(games/synty_demo)
add_subdirectory(games/sandbox)
add_subdirectory(games/melt_demo)
add_subdirectory(games/sea_demo)

# ---------------------------------------------------------------------------
# Standalone smoke tests (no test framework wired in yet — see tests/).
# ---------------------------------------------------------------------------
if(KKE_ENABLE_TESTS)
    add_subdirectory(tests)
endif()
