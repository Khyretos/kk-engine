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
    include/kke/Capabilities.h
    include/kke/Application.h
    include/kke/RmlVulkanRenderInterface.h
    include/kke/GameManifest.h
    include/kke/MarketplaceIndex.h
    include/kke/RmlTextSafety.h
    include/kke/Log.h
    include/kke/HardwareCheck.h
    include/kke/modules/GridModule.h
    include/kke/modules/StatsModule.h
    include/kke/modules/ParticleModule.h
    include/kke/modules/OrbitCameraModule.h
    include/kke/modules/UiModule.h
    include/kke/modules/MarketplaceUiModule.h
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
    spdlog::spdlog
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

if(WIN32)
    target_compile_definitions(kke_engine PUBLIC KKE_PLATFORM_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(kke_engine PUBLIC KKE_PLATFORM_MACOS=1)
else()
    target_compile_definitions(kke_engine PUBLIC KKE_PLATFORM_LINUX=1)
endif()
