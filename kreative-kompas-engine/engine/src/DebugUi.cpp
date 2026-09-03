#include "kke/DebugUi.h"
#include "kke/Window.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"

#include <imgui.h>
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
    ImGui::StyleColorsDark();

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

void DebugUi::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void DebugUi::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void DebugUi::render(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

} // namespace kke
