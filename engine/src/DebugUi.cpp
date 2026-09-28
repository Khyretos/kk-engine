#include "kke/DebugUi.h"
#include "kke/DevTools.h"
#include "kke/Window.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <array>

namespace kke {

namespace {

// ImGui's Vulkan backend needs its own function pointers when the app uses
// VK_NO_PROTOTYPES (volk). volk has already loaded vkGetInstanceProcAddr
// globally, so we just hand that straight to ImGui's loader.
PFN_vkVoidFunction imguiVulkanLoader(const char* functionName, void* userData) {
    VkInstance instance = *reinterpret_cast<VkInstance*>(userData);
    return vkGetInstanceProcAddr(instance, functionName);
}

} // namespace

DebugUi::DebugUi(Window& window, VulkanDevice& device, VkRenderPass renderPass, uint32_t imageCount)
    : m_device(device) {
    std::array<VkDescriptorPoolSize, 1> poolSizes = {
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 64;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VK_CHECK(vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &m_descriptorPool));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
#if defined(__ANDROID__)
    // A phone turns between portrait and landscape, and the panels are
    // placed for the screen they open on: a saved layout from the other
    // orientation would put them off screen or squeeze them.
    ImGui::GetIO().IniFilename = nullptr;
#endif
    ImGui::StyleColorsDark();
    // ImGui's style colors are sRGB values, but its Vulkan backend writes
    // them unconverted into this engine's sRGB swapchain, which encodes
    // them a second time — every panel looked washed out. Converting the
    // palette to linear once here fixes the look without touching the
    // backend. (Colors passed per-widget, e.g. TextColored, are still raw.)
    for (ImVec4& c : ImGui::GetStyle().Colors) {
        auto toLinear = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
        c = ImVec4(toLinear(c.x), toLinear(c.y), toLinear(c.z), c.w);
    }

    // The desktop's or phone's text scale (a phone reports its pixel
    // density: about 3 on a 1440p phone, where ImGui's 13 px font was a
    // smudge). Pixel density itself (Retina, Wayland scaling) ImGui's SDL
    // backend already handles through the framebuffer scale, so only the
    // content scale on top of it is applied: a font rasterised at that size
    // (crisp, unlike FontGlobalScale) and every padding and spacing with it.
    const float density = SDL_GetWindowPixelDensity(window.handle());
    const float scale = density > 0.0f ? SDL_GetWindowDisplayScale(window.handle()) / density : 1.0f;
    if (scale > 1.01f) {
        ImFontConfig font;
        font.SizePixels = std::round(13.0f * scale);
        ImGui::GetIO().Fonts->AddFontDefault(&font);
        ImGui::GetStyle().ScaleAllSizes(scale);
    }

    VkInstance instance = device.instance();
    ImGui_ImplVulkan_LoadFunctions(imguiVulkanLoader, &instance);

    ImGui_ImplSDL3_InitForVulkan(window.handle());

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = device.instance();
    initInfo.PhysicalDevice = device.physicalDevice();
    initInfo.Device = device.device();
    initInfo.QueueFamily = device.queueFamilies().graphics.value();
    initInfo.Queue = device.graphicsQueue();
    initInfo.DescriptorPool = m_descriptorPool;
    initInfo.RenderPass = renderPass;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
}

DebugUi::~DebugUi() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (m_descriptorPool) {
        vkDestroyDescriptorPool(m_device.device(), m_descriptorPool, nullptr);
    }
}

void DebugUi::beginFrame(const ScreenRect& safe, float frameW, float frameH) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    // The safe area as work-area insets, in ImGui's points. NewFrame() just
    // reset the insets being built, so this is the whole of this frame's
    // (a main menu bar then adds its height on top); applied at once too,
    // so windows placed this frame already see it.
    const ImGuiIO& io = ImGui::GetIO();
    if (frameW <= 0.0f || frameH <= 0.0f || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) return;
    const float sx = io.DisplaySize.x / frameW, sy = io.DisplaySize.y / frameH;
    auto* viewport = static_cast<ImGuiViewportP*>(ImGui::GetMainViewport());
    viewport->BuildWorkInsetMin = ImVec2(safe.x * sx, safe.y * sy);
    viewport->BuildWorkInsetMax = ImVec2((frameW - safe.x - safe.w) * sx, (frameH - safe.y - safe.h) * sy);
    viewport->WorkInsetMin = viewport->BuildWorkInsetMin;
    viewport->WorkInsetMax = viewport->BuildWorkInsetMax;
    viewport->UpdateWorkRect();
}

void DebugUi::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void DebugUi::render(VkCommandBuffer cmd) {
    ImGui::Render();
    if (m_visible) ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void DebugUi::setVisible(bool visible) {
    // Shipping builds: the developer panels never show (kke/DevTools.h).
    if constexpr (!dev::kEnabled) visible = false;
    m_visible = visible;
    ImGuiIO& io = ImGui::GetIO();
    if (visible) io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    else io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
}

} // namespace kke
