#include "kke/HardwareCheck.h"
#include "kke/VulkanDevice.h"

#include <vk_mem_alloc.h>
#include <cstdio>
#include <vector>

namespace kke {

namespace {

// Approximates "VRAM" as the total budget of every DEVICE_LOCAL memory
// heap VMA reports. On an integrated/software device (e.g. lavapipe,
// which is what this engine has been verified against throughout) this
// number may be a shared-with-system-RAM figure, not dedicated VRAM in
// the discrete-GPU sense — an honest approximation, not a precise one.
int queryAvailableVramMb(VulkanDevice& device) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(device.physicalDevice(), &memProps);

    std::vector<VmaBudget> budgets(memProps.memoryHeapCount);
    vmaGetHeapBudgets(device.allocator(), budgets.data());

    VkDeviceSize deviceLocalBudget = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            deviceLocalBudget += budgets[i].budget;
        }
    }
    return static_cast<int>(deviceLocalBudget / (1024ull * 1024ull));
}

bool deviceHasFeature(VulkanDevice& device, const std::string& featureName) {
    // Recognized set — see HardwareCheck.h. Deliberately small: only
    // features VulkanDevice actually exposes a query for. Extend both
    // together, not this list alone.
    if (featureName == "largePoints") return device.largePointsSupported();
    return false; // unrecognized — caller treats this as "can't verify," not "supported"
}

void checkTier(const GameManifest::HardwareRequirements& tier, const char* tierName,
               int availableVramMb, uint32_t apiMajor, uint32_t apiMinor,
               VulkanDevice& device, bool& metFlag, std::vector<std::string>& warnings) {
    if (tier.vramMb > 0 && availableVramMb < tier.vramMb) {
        warnings.push_back(std::string(tierName) + " ask for ~" + std::to_string(tier.vramMb) +
                            " MB VRAM; this device reports approximately " +
                            std::to_string(availableVramMb) + " MB available.");
        metFlag = false;
    }

    if (!tier.vulkanApiVersion.empty()) {
        uint32_t reqMajor = 1, reqMinor = 0;
        std::sscanf(tier.vulkanApiVersion.c_str(), "%u.%u", &reqMajor, &reqMinor);
        if (apiMajor < reqMajor || (apiMajor == reqMajor && apiMinor < reqMinor)) {
            warnings.push_back(std::string(tierName) + " ask for Vulkan " + tier.vulkanApiVersion +
                                "; this device reports " + std::to_string(apiMajor) + "." +
                                std::to_string(apiMinor) + ".");
            metFlag = false;
        }
    }

    for (const auto& feature : tier.requiredDeviceFeatures) {
        if (feature != "largePoints") {
            warnings.push_back(std::string(tierName) + " declare an unrecognized required feature '" +
                                feature + "' — can't verify it against real hardware, treating as unmet.");
            metFlag = false;
        } else if (!deviceHasFeature(device, feature)) {
            warnings.push_back(std::string(tierName) + " require the '" + feature +
                                "' device feature, which this device does not support.");
            metFlag = false;
        }
    }
}

} // namespace

HardwareCheckResult checkHardwareRequirements(const GameManifest& manifest, VulkanDevice& device) {
    HardwareCheckResult result;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device.physicalDevice(), &props);
    uint32_t apiMajor = VK_API_VERSION_MAJOR(props.apiVersion);
    uint32_t apiMinor = VK_API_VERSION_MINOR(props.apiVersion);

    int availableVramMb = queryAvailableVramMb(device);

    checkTier(manifest.minimumRequirements, "Minimum requirements",
              availableVramMb, apiMajor, apiMinor, device, result.meetsMinimum, result.warnings);
    checkTier(manifest.recommendedRequirements, "Recommended requirements",
              availableVramMb, apiMajor, apiMinor, device, result.meetsRecommended, result.warnings);

    return result;
}

} // namespace kke
