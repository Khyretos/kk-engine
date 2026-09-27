cmake_minimum_required(VERSION 3.25)
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
option(ENGINE_ENABLE_LUA "Lua 5.4 gameplay scripting (kke::ScriptVM, ScriptModule; docs/SCRIPTING.md)" ON)
option(KKE_ENABLE_NET "Networking over ENet (MIT): kke::NetModule host/join, LAN discovery (docs/NETWORKING.md). Off = offline only; the loopback transport and replication logic still build." ON)
option(KKE_ENABLE_STEAM_AUDIO "Steam Audio (Apache-2.0, Valve) as an optional HRTF backend for headphones: measured HRTFs with elevation, custom SOFA files (docs/AUDIO.md \"Steam Audio\"). Downloads the 150 MB SDK once (Linux and Windows x64) and puts libphonon next to the games." OFF)
option(KKE_ENABLE_VOICE "Voice chat: Opus (BSD) codec, VoiceModule (docs/NETWORKING.md \"Voice\"). Needs KKE_ENABLE_NET." ON)
option(KKE_ENABLE_VALKEY "Storage on a Valkey/Redis server (hiredis, BSD; docs/STORAGE.md)" ON)
option(KKE_ENABLE_POSTGRES "Storage in PostgreSQL (needs the system libpq, e.g. apt install libpq-dev; docs/STORAGE.md)" OFF)
option(ENGINE_HEADLESS_TEST "Build the headless smoke-test target" ON)
option(KKE_ENABLE_TESTS "Build the unit test suite (tests/)" ON)
option(KKE_ENABLE_BENCHMARKS "Build kke_bench, the headless benchmark suite (benchmarks/, docs/BENCHMARKS.md)" ON)
option(KKE_ENABLE_JOLT "Build the Jolt Physics rigid-body/collision layer (MIT): static world collision, rigid bodies, character controller. See ACTION_PLAN.md 1.1." ON)
option(KKE_ENABLE_FEMFX "Build the vendored, patched AMD FEMFX deformable-physics library. See docs/HISTORY.md 'Physics: AMD FEMFX integration'." OFF)
option(KKE_ENABLE_COVERAGE "Instrument builds for code coverage (gcc/clang --coverage). See docs/HISTORY.md 'Test suite & coverage'." OFF)
option(KKE_WARNINGS_AS_ERRORS "GCC/Clang: fail the build on any -Wall -Wextra warning in the engine, games, tools or tests (third-party code excluded). CI turns this on." OFF)
option(KKE_SHIPPING "Shipping build of a game: compiles out the developer tools players must not reach (debug panels, the Lua console, script hot reload, debug environment switches such as virtual input). See docs/ANTI_CHEAT.md." OFF)
option(KKE_ENABLE_GPU_PROFILER "Request the VulkanProfiler layer (VK_LAYER_PROFILER_unified) at instance creation if installed. See docs/HISTORY.md 'GPU profiler (VulkanProfiler) integration'." OFF)

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
    # 1.4.304: first release whose cmake_minimum_required has a policy
    # range, so CMake 4 doesn't warn "Compatibility with CMake < 3.10".
    GIT_TAG        vulkan-sdk-1.4.304.0
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
    SYSTEM
)
FetchContent_MakeAvailable(vma)

# ---------------------------------------------------------------------------
# SDL3 — windowing + input. Built from source so it's identical on every platform.
# ---------------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
# SDL 3.4 fails the configure when an optional X11 extension is missing
# (3.2 skipped it quietly). Screensaver (old-X-server screensaver
# inhibit) and XTest (synthetic input) aren't used by the engine.
# libudev (joystick hot-plug) and dbus should be installed: see docs/HISTORY.md.
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)      # synthetic input injection: not needed
# Sound goes through miniaudio (AudioModule), never SDL: without this SDL
# probes for ALSA/PulseAudio/JACK at configure time and warns when the dev
# packages are missing, for a subsystem nobody calls.
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    TRUE
)
# SDL's Wayland protocol XML needs wayland-scanner 1.26+ (see the file).
include(cmake/wayland_scanner.cmake)
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
    GIT_TAG        1.0.3 # 1.0.2+: no CMake < 3.10 deprecation warning
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# ---------------------------------------------------------------------------
# stb — header-only utilities (stb_image now, more of the stb_* family later).
# ---------------------------------------------------------------------------
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    # stb has no releases: pinned to a commit so every build compiles (and
    # docs/DEPENDENCIES.md describes) the same code. No GIT_SHALLOW: a
    # shallow clone can only check out a branch or tag.
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20 # master, 2026-08-01
)
FetchContent_MakeAvailable(stb)
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

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
        GIT_TAG        VER-2-14-3 # 2.14: no CMake < 3.10 deprecation warning
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
    # Makes data-for lists update before their rows (see the script for the
    # bug: "Data array index out of bounds" warnings when a list shrinks).
    PATCH_COMMAND  ${CMAKE_COMMAND} -DRMLUI_SOURCE_DIR=<SOURCE_DIR> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_rmlui.cmake
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(rmlui)

# ---------------------------------------------------------------------------
# nlohmann/json — the engine's one JSON library. Foundational rather than
# optional: game.json manifests, the marketplace index, and (eventually)
# save data are all specified to be JSON — see GameManifest.h and the
# docs/HISTORY.md's "Game folder convention & marketplace" section.
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
# Monocypher — BLAKE2b hashing and EdDSA signatures for sealed game data
# (kke/PackSeal.h, docs/ANTI_CHEAT.md). BSD-2 / CC0, one .c/.h pair, no
# dependencies, constant-time. Always optimized: hashing a whole pack at
# -O0 would make startup checks needlessly slow.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        4.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(monocypher)
add_library(kke_monocypher STATIC ${monocypher_SOURCE_DIR}/src/monocypher.c)
target_include_directories(kke_monocypher SYSTEM PUBLIC ${monocypher_SOURCE_DIR}/src)
set_target_properties(kke_monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(kke_monocypher PRIVATE $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>)

# ---------------------------------------------------------------------------
# yaml-cpp — YAML for data files that also come as JSON (kke/DataFile.h):
# pack.yml, mods.yml, game.yml work the same as their .json twins. MIT.
# Only used inside the engine library (PRIVATE), converted to JSON there.
# ---------------------------------------------------------------------------
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    yaml_cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        yaml-cpp-0.9.0
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(yaml_cpp)
set_target_properties(yaml-cpp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# meshoptimizer — mesh LODs for crowds (kke/MeshLod.h): welds duplicate
# vertices and simplifies a character to a fraction of its triangles while
# keeping each vertex's bone weights. MIT, Arseny Kapoulkine. Only used
# inside the engine library (PRIVATE).
# ---------------------------------------------------------------------------
set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v1.3
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(meshoptimizer)
set_target_properties(meshoptimizer PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# miniaudio — audio output device on every platform (ALSA/PulseAudio/
# PipeWire/JACK, WASAPI, CoreAudio, AAudio/OpenSL, Web Audio) plus WAV/
# FLAC/MP3 decoding. Public domain / MIT-0, one .c/.h pair. Only the
# device and decoders are used: mixing is our own (kke::AudioMixer,
# docs/AUDIO.md), so the engine/node-graph/resource-manager parts are compiled
# out. Falls back to its null device when there's no sound card.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.22
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  kke-no-cmake # fetch only: its own CMakeLists builds examples/tests we don't want
)
FetchContent_MakeAvailable(miniaudio)
add_library(kke_miniaudio STATIC ${miniaudio_SOURCE_DIR}/miniaudio.c)
target_include_directories(kke_miniaudio PUBLIC ${miniaudio_SOURCE_DIR})
target_compile_definitions(kke_miniaudio PUBLIC MA_NO_ENGINE MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENCODING MA_NO_GENERATION)
set_target_properties(kke_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(kke_miniaudio PRIVATE $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>)
find_package(Threads REQUIRED)
target_link_libraries(kke_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(kke_miniaudio PUBLIC m)
endif()

# ---------------------------------------------------------------------------
# GoogleTest — the unit test framework for this engine's pure-logic code
# (manifest parsing, marketplace dedup, text-escaping — see docs/HISTORY.md "Test
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
# Lua 5.4 — gameplay scripting (kke::ScriptVM, ScriptModule; docs/SCRIPTING.md).
# MIT licensed, ~30 C files. Compiled as C++ on purpose: Lua then raises
# errors as C++ exceptions instead of longjmp, so an error thrown inside
# an engine binding unwinds its C++ objects properly.
# ---------------------------------------------------------------------------
if(ENGINE_ENABLE_LUA)
    FetchContent_Declare(
        lua
        URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
        URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(lua)
    file(GLOB LUA_CORE_SOURCES ${lua_SOURCE_DIR}/src/*.c)
    list(FILTER LUA_CORE_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua)\\.c$")
    set_source_files_properties(${LUA_CORE_SOURCES} PROPERTIES LANGUAGE CXX)
    add_library(lua_lib STATIC ${LUA_CORE_SOURCES})
    target_include_directories(lua_lib PUBLIC ${lua_SOURCE_DIR}/src)
    set_target_properties(lua_lib PROPERTIES POSITION_INDEPENDENT_CODE ON LINKER_LANGUAGE CXX)
    if(UNIX)
        target_compile_definitions(lua_lib PRIVATE LUA_USE_POSIX)
    endif()
    # Always optimized: nobody steps through the Lua VM, and scripts run in it.
    target_compile_options(lua_lib PRIVATE $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>)
endif()

# ---------------------------------------------------------------------------
# ENet 1.3.18 — the default network transport (docs/NETWORKING.md). MIT, plain C,
# every OS. Built from its sources here (its own CMakeLists predates the
# CMake versions we require); the HAS_* feature macros are what its
# configure script would find on any current Linux/macOS libc.
# ---------------------------------------------------------------------------
if(KKE_ENABLE_NET)
    FetchContent_Declare(
        enet
        GIT_REPOSITORY https://github.com/lsalzman/enet.git
        GIT_TAG        v1.3.18
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  kke-no-cmake
    )
    FetchContent_MakeAvailable(enet)
    set(KKE_ENET_SOURCES callbacks.c compress.c host.c list.c packet.c peer.c protocol.c)
    if(WIN32)
        list(APPEND KKE_ENET_SOURCES win32.c)
    else()
        list(APPEND KKE_ENET_SOURCES unix.c)
    endif()
    list(TRANSFORM KKE_ENET_SOURCES PREPEND ${enet_SOURCE_DIR}/)
    add_library(kke_enet STATIC ${KKE_ENET_SOURCES})
    target_include_directories(kke_enet SYSTEM PUBLIC ${enet_SOURCE_DIR}/include)
    set_target_properties(kke_enet PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(WIN32)
        target_link_libraries(kke_enet PUBLIC ws2_32 winmm)
    else()
        target_compile_definitions(kke_enet PRIVATE HAS_FCNTL=1 HAS_POLL=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1
                                   HAS_INET_PTON=1 HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1 HAS_SOCKLEN_T=1)
    endif()
endif()

# ---------------------------------------------------------------------------
# Recast & Detour (recastnavigation, zlib): navigation meshes and path
# finding for the AI core (kke::ai::NavMesh, docs/AI.md). Only the four
# runtime libraries are compiled, directly (its own CMake also builds the
# demo and tests).
# ---------------------------------------------------------------------------
FetchContent_Declare(
    recastnavigation
    GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
    GIT_TAG        v1.6.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  kke-not-a-dir # sources only; see above
)
FetchContent_MakeAvailable(recastnavigation)
file(GLOB KKE_RECAST_SOURCES
    ${recastnavigation_SOURCE_DIR}/Recast/Source/*.cpp
    ${recastnavigation_SOURCE_DIR}/Detour/Source/*.cpp
    ${recastnavigation_SOURCE_DIR}/DetourCrowd/Source/*.cpp
    ${recastnavigation_SOURCE_DIR}/DetourTileCache/Source/*.cpp)
add_library(kke_recast STATIC ${KKE_RECAST_SOURCES})
target_include_directories(kke_recast SYSTEM PUBLIC
    ${recastnavigation_SOURCE_DIR}/Recast/Include
    ${recastnavigation_SOURCE_DIR}/Detour/Include
    ${recastnavigation_SOURCE_DIR}/DetourCrowd/Include
    ${recastnavigation_SOURCE_DIR}/DetourTileCache/Include)
set_target_properties(kke_recast PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Optimized even in Debug: building a navmesh at -O0 takes seconds.
if(NOT MSVC)
    target_compile_options(kke_recast PRIVATE $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>)
endif()

# ---------------------------------------------------------------------------
# SQLite — the built-in store (kke::storage, docs/STORAGE.md): public
# domain, one C file, nothing for players or server owners to install.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    sqlite
    URL      https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032
)
FetchContent_MakeAvailable(sqlite)
add_library(kke_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(kke_sqlite SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
# Thread-safe, no loadable extensions (nothing outside the engine runs in
# it), double-quoted strings off (a typo'd column name is an error).
target_compile_definitions(kke_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0 SQLITE_DEFAULT_WAL_SYNCHRONOUS=1)
set_target_properties(kke_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(kke_sqlite PRIVATE $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>)
target_link_libraries(kke_sqlite PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# ---------------------------------------------------------------------------
# hiredis — the Valkey/Redis client for kke::storage::ValkeyStore (BSD-3).
# Its sources are compiled here directly: its own CMakeLists warns on
# current CMake, and we want no warnings in the build log at all.
# ---------------------------------------------------------------------------
if(KKE_ENABLE_VALKEY)
    FetchContent_Declare(
        hiredis
        GIT_REPOSITORY https://github.com/redis/hiredis.git
        GIT_TAG        v1.2.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  kke-no-cmake
    )
    FetchContent_MakeAvailable(hiredis)
    add_library(kke_hiredis STATIC
        ${hiredis_SOURCE_DIR}/alloc.c ${hiredis_SOURCE_DIR}/hiredis.c ${hiredis_SOURCE_DIR}/net.c
        ${hiredis_SOURCE_DIR}/read.c ${hiredis_SOURCE_DIR}/sds.c ${hiredis_SOURCE_DIR}/sockcompat.c ${hiredis_SOURCE_DIR}/async.c)
    target_include_directories(kke_hiredis SYSTEM PUBLIC ${hiredis_SOURCE_DIR})
    set_target_properties(kke_hiredis PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(WIN32)
        target_link_libraries(kke_hiredis PUBLIC ws2_32)
    endif()
endif()
if(KKE_ENABLE_POSTGRES)
    find_package(PostgreSQL REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Opus — the voice chat codec (BSD-3, https://opus-codec.org): 48 kHz mono,
# 20 ms frames, in-band FEC. Only the library; its tests and tools are off.
# ---------------------------------------------------------------------------
if(KKE_ENABLE_NET AND KKE_ENABLE_VOICE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        opus
        GIT_REPOSITORY https://github.com/xiph/opus.git
        GIT_TAG        v1.5.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(opus)
    set_target_properties(opus PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# ---------------------------------------------------------------------------
# Cleaning up the microphone for voice chat (kke::voice::VoiceCleaner):
#   RNNoise (BSD-3, Xiph, https://github.com/xiph/rnnoise): noise
#   suppression by a small neural network. v0.1.1 carries its model in
#   the source (rnn_data.c, 416 KB), where later versions download a much
#   bigger one at build time.
#   SpeexDSP (BSD-3, Xiph, https://github.com/xiph/speexdsp): the echo
#   canceller (MDF) and its residual echo suppressor.
# Both are autotools projects; the few C files we need are compiled here
# directly (like hiredis), floating point, with SpeexDSP's small FFT.
# ---------------------------------------------------------------------------
if(KKE_ENABLE_NET AND KKE_ENABLE_VOICE)
    FetchContent_Declare(
        rnnoise
        GIT_REPOSITORY https://github.com/xiph/rnnoise.git
        GIT_TAG        v0.1.1
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  kke-no-cmake
    )
    FetchContent_Declare(
        speexdsp
        GIT_REPOSITORY https://github.com/xiph/speexdsp.git
        GIT_TAG        SpeexDSP-1.2.1
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  kke-no-cmake
    )
    FetchContent_MakeAvailable(rnnoise speexdsp)
    add_library(kke_rnnoise STATIC
        ${rnnoise_SOURCE_DIR}/src/denoise.c ${rnnoise_SOURCE_DIR}/src/rnn.c ${rnnoise_SOURCE_DIR}/src/rnn_data.c
        ${rnnoise_SOURCE_DIR}/src/pitch.c ${rnnoise_SOURCE_DIR}/src/kiss_fft.c ${rnnoise_SOURCE_DIR}/src/celt_lpc.c)
    target_include_directories(kke_rnnoise SYSTEM PUBLIC ${rnnoise_SOURCE_DIR}/include)
    set_target_properties(kke_rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(kke_rnnoise PRIVATE $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>)
    if(NOT WIN32)
        target_link_libraries(kke_rnnoise PRIVATE m)
    endif()

    # speexdsp_config_types.h is made by its configure script; the same, here.
    set(KKE_SPEEXDSP_GEN ${CMAKE_BINARY_DIR}/speexdsp-gen)
    file(WRITE ${KKE_SPEEXDSP_GEN}/speexdsp_config_types.h.new
        "#ifndef __SPEEX_TYPES_H__\n#define __SPEEX_TYPES_H__\n#include <stdint.h>\n"
        "typedef int16_t spx_int16_t;\ntypedef uint16_t spx_uint16_t;\ntypedef int32_t spx_int32_t;\ntypedef uint32_t spx_uint32_t;\n#endif\n")
    configure_file(${KKE_SPEEXDSP_GEN}/speexdsp_config_types.h.new ${KKE_SPEEXDSP_GEN}/speexdsp_config_types.h COPYONLY)
    # Its warnings (the echo filter resetting itself when it diverged, ...)
    # go to the engine log instead of stderr: kke_speex_warning() in
    # engine/src/voice/VoiceCleaner.cpp.
    file(WRITE ${KKE_SPEEXDSP_GEN}/kke_speex_log.h.new
        "#ifndef KKE_SPEEX_LOG_H\n#define KKE_SPEEX_LOG_H\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
        "void kke_speex_warning(const char* text, int value, int hasValue);\n#ifdef __cplusplus\n}\n#endif\n"
        "#define OVERRIDE_SPEEX_WARNING\n#define OVERRIDE_SPEEX_WARNING_INT\n"
        "static inline void speex_warning(const char* str) { kke_speex_warning(str, 0, 0); }\n"
        "static inline void speex_warning_int(const char* str, int val) { kke_speex_warning(str, val, 1); }\n#endif\n")
    configure_file(${KKE_SPEEXDSP_GEN}/kke_speex_log.h.new ${KKE_SPEEXDSP_GEN}/kke_speex_log.h COPYONLY)
    add_library(kke_speexdsp STATIC
        ${speexdsp_SOURCE_DIR}/libspeexdsp/mdf.c ${speexdsp_SOURCE_DIR}/libspeexdsp/preprocess.c
        ${speexdsp_SOURCE_DIR}/libspeexdsp/fftwrap.c ${speexdsp_SOURCE_DIR}/libspeexdsp/smallft.c
        ${speexdsp_SOURCE_DIR}/libspeexdsp/filterbank.c)
    target_include_directories(kke_speexdsp SYSTEM PUBLIC ${speexdsp_SOURCE_DIR}/include ${KKE_SPEEXDSP_GEN})
    target_compile_definitions(kke_speexdsp PRIVATE FLOATING_POINT USE_SMALLFT EXPORT=)
    if(MSVC)
        target_compile_options(kke_speexdsp PRIVATE /FI${KKE_SPEEXDSP_GEN}/kke_speex_log.h)
    else()
        target_compile_options(kke_speexdsp PRIVATE -include ${KKE_SPEEXDSP_GEN}/kke_speex_log.h)
    endif()
    set_target_properties(kke_speexdsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(kke_speexdsp PRIVATE $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:-O2>)
    if(NOT WIN32)
        target_link_libraries(kke_speexdsp PRIVATE m)
    endif()
endif()

# ---------------------------------------------------------------------------
# Steam Audio (Apache-2.0, https://github.com/ValveSoftware/steam-audio): the
# prebuilt SDK, only for its HRTF (kke::SteamAudioSpatializer). Its own
# licence isn't in the SDK zip; it is fetched next to it (pinned by hash) so
# packages can carry it. libphonon is copied into bin/ and found there
# ($ORIGIN) by every game, in the build tree and in packages alike.
# ---------------------------------------------------------------------------
if(KKE_ENABLE_STEAM_AUDIO)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "KKE_ENABLE_STEAM_AUDIO: the Steam Audio SDK only ships 64-bit x86 libraries for Linux and Windows")
    endif()
    FetchContent_Declare(
        steamaudio
        URL      https://github.com/ValveSoftware/steam-audio/releases/download/v4.6.1/steamaudio_4.6.1.zip
        URL_HASH SHA256=e9c56314f1066f631f193b0cb0ad886bd32daa3a9758b39c25a614ca453149a7
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(steamaudio)
    set(_kke_sa_license ${steamaudio_SOURCE_DIR}/LICENSE.md)
    if(NOT EXISTS ${_kke_sa_license})
        file(DOWNLOAD https://raw.githubusercontent.com/ValveSoftware/steam-audio/v4.6.1/LICENSE.md ${_kke_sa_license}
             EXPECTED_HASH SHA256=cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30 STATUS _kke_sa_status)
        list(GET _kke_sa_status 0 _kke_sa_code)
        if(NOT _kke_sa_code EQUAL 0)
            file(REMOVE ${_kke_sa_license})
            message(FATAL_ERROR "KKE_ENABLE_STEAM_AUDIO: could not download Steam Audio's LICENSE.md: ${_kke_sa_status}")
        endif()
    endif()
    add_library(kke_steamaudio SHARED IMPORTED GLOBAL)
    if(WIN32)
        set(_kke_sa_lib ${steamaudio_SOURCE_DIR}/lib/windows-x64/phonon.dll)
        set_target_properties(kke_steamaudio PROPERTIES IMPORTED_LOCATION ${_kke_sa_lib}
                              IMPORTED_IMPLIB ${steamaudio_SOURCE_DIR}/lib/windows-x64/phonon.lib)
    else()
        set(_kke_sa_lib ${steamaudio_SOURCE_DIR}/lib/linux-x64/libphonon.so)
        set_target_properties(kke_steamaudio PROPERTIES IMPORTED_LOCATION ${_kke_sa_lib} IMPORTED_SONAME libphonon.so)
        # Next to the executable, in the build tree and in packages.
        target_link_options(kke_steamaudio INTERFACE "LINKER:-rpath,$ORIGIN")
    endif()
    if(NOT EXISTS ${_kke_sa_lib})
        message(FATAL_ERROR "KKE_ENABLE_STEAM_AUDIO: ${_kke_sa_lib} is missing from the SDK")
    endif()
    target_include_directories(kke_steamaudio SYSTEM INTERFACE ${steamaudio_SOURCE_DIR}/include)
    file(COPY ${_kke_sa_lib} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
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
# (the world's collision, rocks, debris, players): docs/SCALING.md.
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
# hitting this ordering issue rather than assuming it, see docs/HISTORY.md
# "Physics: AMD FEMFX integration."
if(KKE_ENABLE_FEMFX)
    add_subdirectory(external/FEMFX)
endif()

# Everything below is this project's own code: full warnings, and every
# one of them is fixed, not silenced. Third-party code above keeps its
# own flags, and its headers are SYSTEM includes so they don't warn in
# our translation units.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
    if(KKE_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

add_subdirectory(engine)

# (The CGAL-based offline tetrahedralizer that used to live in tools/ was
# removed on 2026-09-26: kke::voxelizeToTets does the job at runtime on any
# mesh, including open ones CGAL rejects, and CGAL's GPL licence was a risk
# for an engine that powers a marketplace. See docs/HISTORY.md "Content pipeline".)

# The physics multithreading benchmark — links femfx directly (not
# kke_engine), so it only needs KKE_ENABLE_FEMFX. See tools/physics_benchmark/
# main.cpp's own top comment for why this tool exists: this sandbox
# has exactly 1 CPU core, so no real speedup could ever be measured
# here — this tool is what lets that be measured honestly on whatever
# hardware actually has more than one.
add_subdirectory(tools/model_info)
add_subdirectory(tools/seal)
add_subdirectory(tools/license)
add_subdirectory(tools/packs)
if(KKE_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
add_subdirectory(tools/audio_preview)
if(KKE_ENABLE_NET)
    add_subdirectory(tools/kke_server)
endif()
if(KKE_ENABLE_FEMFX)
    add_subdirectory(tools/physics_benchmark)
    add_subdirectory(tools/physics_lab)
endif()

# ---------------------------------------------------------------------------
# App executable (the actual window + cube demo)
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Games — each subdirectory of games/ is a self-contained "game folder"
# (see docs/HISTORY.md "Game folder convention & marketplace"). Only one is built
# by default right now; a real multi-game build (or marketplace-driven
# discovery) is tracked in the Roadmap, not implemented yet — this single
# add_subdirectory is today's honest state, not a placeholder pretending
# to be more general than it is.
# ---------------------------------------------------------------------------
add_subdirectory(games/kke_demo_game)
if(KKE_ENABLE_JOLT)
    add_subdirectory(games/showcase)
endif()

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
add_subdirectory(games/jiggle_demo)
add_subdirectory(games/sea_demo)
# Every audio case, one station each: walls and floors are Jolt bodies
# (the sound's rays hit them), so it needs Jolt.
if(KKE_ENABLE_JOLT)
    add_subdirectory(games/audio_demo)
endif()
# Climb Race: a generated mountain face, climbed hold by hold, against a
# rival (games/climb_race/README.md). Jolt: the rock and the climbers.
if(KKE_ENABLE_JOLT)
    add_subdirectory(games/climb_race)
endif()
# Duel: a one-on-one fist fight on kke::Combatant (games/duel/README.md).
# Jolt: the ring, the fighters and knockdown ragdolls.
if(KKE_ENABLE_JOLT)
    add_subdirectory(games/duel)
endif()

# The starter template (docs/tutorials/getting-started.md), and games made
# from it with tools/new_game, which lists them in games/my_games.cmake.
if(KKE_ENABLE_JOLT AND ENGINE_ENABLE_LUA)
    add_subdirectory(games/template)
    include(games/my_games.cmake)
    # The cookbook (docs/cookbook/): cameras, animation and IK recipes.
    add_subdirectory(games/cookbook)
endif()

# ---------------------------------------------------------------------------
# Standalone smoke tests (no test framework wired in yet — see tests/).
# ---------------------------------------------------------------------------
if(KKE_ENABLE_TESTS)
    add_subdirectory(tests)
endif()
