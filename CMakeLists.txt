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

# ---------------------------------------------------------------------------
# Vulkan headers/loader (system SDK — validated via find_package)
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)

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
    FetchContent_MakeAvailable(googletest)
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

function(engine_add_shader TARGET SHADER_SOURCE)
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${SHADER_NAME}.spv)
    get_filename_component(SHADER_OUTPUT_DIR ${SHADER_OUTPUT} DIRECTORY)

    if(GLSLC_EXECUTABLE)
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling shader ${SHADER_NAME} (glslc)"
        )
    elseif(GLSLANG_VALIDATOR_EXECUTABLE)
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
            COMMAND ${GLSLANG_VALIDATOR_EXECUTABLE} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling shader ${SHADER_NAME} (glslangValidator)"
        )
    else()
        message(FATAL_ERROR "Neither glslc nor glslangValidator found — install the Vulkan SDK or glslang-tools.")
    endif()

    string(REPLACE "." "_" SHADER_TARGET_NAME "shader_${SHADER_NAME}")
    add_custom_target(${SHADER_TARGET_NAME} DEPENDS ${SHADER_OUTPUT})
    add_dependencies(${TARGET} ${SHADER_TARGET_NAME})
endfunction()

# ---------------------------------------------------------------------------
# Engine core library
# ---------------------------------------------------------------------------
# FEMFX (if enabled) must be added before engine/, since engine's own
# CMakeLists.txt links against the femfx target — found by actually
# hitting this ordering issue rather than assuming it, see README
# "Physics: AMD FEMFX integration."
if(KKE_ENABLE_FEMFX)
    add_subdirectory(external/FEMFX)
endif()

add_subdirectory(engine)

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

# ---------------------------------------------------------------------------
# Standalone smoke tests (no test framework wired in yet — see tests/).
# ---------------------------------------------------------------------------
if(KKE_ENABLE_TESTS)
    add_subdirectory(tests)
endif()
