#include "kke/VulkanDevice.h"
#include "kke/Window.h"
#include "kke/VulkanCheck.h"
#include "kke/Log.h"

#if KKE_ENABLE_GPU_PROFILER
#include <VkProfilerEXT.h>
#endif

#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace kke {

namespace {

const std::vector<const char*> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// lstalmir/VulkanProfiler — see README "GPU profiler (VulkanProfiler)
// integration" for the full story: what this layer actually is, how it
// was verified, and what's still not done. Not built/installed by this
// repo — it's a separate system-level Vulkan layer, same as the
// validation layer above. This constant, the availability check, and
// the enable logic below all work correctly whether or not it's
// actually installed, exactly like validation layers already do.
const char* kGpuProfilerLayerName = "VK_LAYER_PROFILER_unified";

const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

bool isInstanceLayerAvailable(const char* layerName) {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const auto& props : availableLayers) {
        if (std::strcmp(layerName, props.layerName) == 0) return true;
    }
    return false;
}

bool checkValidationLayerSupport() {
    for (const char* layerName : kValidationLayers) {
        if (!isInstanceLayerAvailable(layerName)) return false;
    }
    return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        log::get("VulkanDevice")->error("{}", data->pMessage);
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        log::get("VulkanDevice")->warn("{}", data->pMessage);
    }
    return VK_FALSE;
}

} // namespace

VulkanDevice::VulkanDevice(Window& window, bool enableValidation)
    : m_window(window), m_validationEnabled(enableValidation) {
    VK_CHECK(volkInitialize());

    createInstance(enableValidation);
    volkLoadInstance(m_instance);

    if (enableValidation) {
        setupDebugMessenger();
    }

    m_surface = m_window.createSurface(m_instance);

    pickPhysicalDevice();
    createLogicalDevice(enableValidation);
    volkLoadDevice(m_device);

    createAllocator();
    createCommandPool();
    loadGpuProfilerFunctions();
}

VulkanDevice::~VulkanDevice() {
    if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    if (m_allocator) vmaDestroyAllocator(m_allocator);
    if (m_device) vkDestroyDevice(m_device, nullptr);
    if (m_debugMessenger) vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance) vkDestroyInstance(m_instance, nullptr);
}

void VulkanDevice::createInstance(bool enableValidation) {
    if (enableValidation && !checkValidationLayerSupport()) {
        log::get("VulkanDevice")->warn("validation layers requested but not available; continuing without them");
        enableValidation = false;
        m_validationEnabled = false;
    }

    std::vector<const char*> layers;
    if (enableValidation) {
        layers.insert(layers.end(), kValidationLayers.begin(), kValidationLayers.end());
    }

#if KKE_ENABLE_GPU_PROFILER
    // See the comment on kGpuProfilerLayerName: this degrades gracefully
    // to "not enabled, logged, nothing else changes" if the layer isn't
    // actually installed on this machine — same pattern as validation
    // layers just above.
    m_gpuProfilerEnabled = isInstanceLayerAvailable(kGpuProfilerLayerName);
    if (m_gpuProfilerEnabled) {
        layers.push_back(kGpuProfilerLayerName);
        log::get("VulkanDevice")->info("GPU profiler layer '{}' found and will be enabled", kGpuProfilerLayerName);
    } else {
        log::get("VulkanDevice")->warn(
            "KKE_ENABLE_GPU_PROFILER is on but layer '{}' isn't installed — see README "
            "'GPU profiler (VulkanProfiler) integration' for how to build/install it. Continuing without it.",
            kGpuProfilerLayerName);
    }
#endif

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "3dco-engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "3dco-engine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    auto extensions = m_window.getRequiredInstanceExtensions();
    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

#if KKE_ENABLE_GPU_PROFILER
    // Requests VK_PROFILER_MODE_PER_DRAWCALL_EXT — the finest-grained
    // sampling mode the layer supports, matching "check everything up to
    // the draw calls." Only meaningful if m_gpuProfilerEnabled is true;
    // harmless (ignored) otherwise since no layer is present to read it.
    VkLayerSettingEXT profilerSetting{};
    profilerSetting.pLayerName = kGpuProfilerLayerName;
    profilerSetting.pSettingName = "sampling_mode";
    profilerSetting.type = VK_LAYER_SETTING_TYPE_STRING_EXT;
    profilerSetting.valueCount = 1;
    static const char* kDrawcallMode = "drawcall";
    profilerSetting.pValues = &kDrawcallMode;

    VkLayerSettingsCreateInfoEXT layerSettingsInfo{};
    layerSettingsInfo.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
    layerSettingsInfo.settingCount = 1;
    layerSettingsInfo.pSettings = &profilerSetting;

    if (m_gpuProfilerEnabled) {
        createInfo.pNext = &layerSettingsInfo;
    }
#endif

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));
}

void VulkanDevice::setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    if (vkCreateDebugUtilsMessengerEXT) {
        VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger));
    }
}

QueueFamilyIndices VulkanDevice::findQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilyIndices indices;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.present = i;
        }

        if (indices.isComplete()) break;
    }

    return indices;
}

bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice device) const {
    QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.isComplete()) return false;

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());

    std::set<std::string> required(kDeviceExtensions.begin(), kDeviceExtensions.end());
    for (const auto& ext : available) {
        required.erase(ext.extensionName);
    }
    if (!required.empty()) return false;

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

    return formatCount > 0 && presentModeCount > 0;
}

void VulkanDevice::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) {
        throw std::runtime_error("no GPUs with Vulkan support found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    for (auto device : devices) {
        if (isDeviceSuitable(device)) {
            m_physicalDevice = device;
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("no suitable GPU found");
    }

    m_queueFamilies = findQueueFamilies(m_physicalDevice);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    m_timestampPeriodNs = static_cast<double>(props.limits.timestampPeriod);
    log::get("VulkanDevice")->info("using device: {}", props.deviceName);
}

void VulkanDevice::createLogicalDevice(bool enableValidation) {
    std::set<uint32_t> uniqueFamilies = {
        m_queueFamilies.graphics.value(),
        m_queueFamilies.present.value()
    };

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float priority = 1.0f;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &priority;
        queueCreateInfos.push_back(info);
    }

    VkPhysicalDeviceFeatures supportedFeatures{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &supportedFeatures);

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.largePoints = supportedFeatures.largePoints;   // particle point-sprites use gl_PointSize > 1.0
    deviceFeatures.wideLines = supportedFeatures.wideLines;       // handy for future debug-line rendering
    m_largePointsSupported = (supportedFeatures.largePoints == VK_TRUE);

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

    if (enableValidation) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
        createInfo.ppEnabledLayerNames = kValidationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));

    vkGetDeviceQueue(m_device, m_queueFamilies.graphics.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueFamilies.present.value(), 0, &m_presentQueue);
}

void VulkanDevice::createAllocator() {
    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator));
}

void VulkanDevice::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilies.graphics.value();

    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool));
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type");
}

void VulkanDevice::loadGpuProfilerFunctions() {
#if KKE_ENABLE_GPU_PROFILER
    if (!m_gpuProfilerEnabled) return;

    // These are the layer's own extension functions — volk's generated
    // loader has no idea they exist, so they're loaded by hand exactly
    // the way any Vulkan extension function is meant to be: through
    // vkGetDeviceProcAddr, not linked against directly.
    m_vkGetProfilerFrameDataEXT =
        reinterpret_cast<PFN_vkGetProfilerFrameDataEXT>(vkGetDeviceProcAddr(m_device, "vkGetProfilerFrameDataEXT"));
    m_vkFreeProfilerFrameDataEXT =
        reinterpret_cast<PFN_vkFreeProfilerFrameDataEXT>(vkGetDeviceProcAddr(m_device, "vkFreeProfilerFrameDataEXT"));

    if (!m_vkGetProfilerFrameDataEXT || !m_vkFreeProfilerFrameDataEXT) {
        log::get("VulkanDevice")->warn(
            "GPU profiler layer is active but vkGetProfilerFrameDataEXT/vkFreeProfilerFrameDataEXT "
            "could not be loaded — frame data queries will report invalid. The layer's own overlay "
            "should still work regardless.");
        m_gpuProfilerEnabled = false;
    }
#endif
}

namespace {
#if KKE_ENABLE_GPU_PROFILER
// Recursively counts actual leaf commands (draws/dispatches/copies —
// VK_PROFILER_REGION_TYPE_COMMAND_EXT) in the layer's returned region
// tree, skipping over the render-pass/pipeline/command-buffer grouping
// levels above them. This is the real "how many draw calls" number,
// not a proxy for it.
uint32_t countCommandRegions(const VkProfilerRegionDataEXT& region) {
    uint32_t count = (region.regionType == VK_PROFILER_REGION_TYPE_COMMAND_EXT) ? 1 : 0;
    for (uint32_t i = 0; i < region.subregionCount; ++i) {
        count += countCommandRegions(region.pSubregions[i]);
    }
    return count;
}
#endif
} // namespace

VulkanDevice::GpuProfilerFrameSummary VulkanDevice::queryGpuProfilerFrameSummary() const {
    GpuProfilerFrameSummary summary;
#if KKE_ENABLE_GPU_PROFILER
    if (!m_gpuProfilerEnabled || !m_vkGetProfilerFrameDataEXT || !m_vkFreeProfilerFrameDataEXT) {
        return summary;
    }

    VkProfilerDataEXT data{};
    data.sType = VK_STRUCTURE_TYPE_PROFILER_DATA_EXT;

    if (m_vkGetProfilerFrameDataEXT(m_device, &data) == VK_SUCCESS) {
        summary.valid = true;
        // "duration" isn't documented with explicit units in
        // VkProfilerEXT.h; inferred as milliseconds from matching this
        // engine's own timestamp-query GPU timing and the layer's own
        // overlay display convention (see README) — not an assumption
        // taken from an authoritative doc string, worth remembering if
        // the numbers ever look off by 1000x.
        summary.frameDurationMs = data.frame.duration;
        summary.commandCount = countCommandRegions(data.frame);
        m_vkFreeProfilerFrameDataEXT(m_device, &data);
    }
#endif
    return summary;
}

} // namespace kke
