#include "kke/ShaderUtils.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace kke {

VkShaderModule loadShaderModule(VulkanDevice& device, const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open shader file: " + path);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device.device(), &createInfo, nullptr, &module));
    return module;
}

// Loads a compiled .spv file into 32-bit words.
std::vector<uint32_t> loadSpirv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open shader file: " + path);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    if (fileSize % 4 != 0) {
        throw std::runtime_error("shader file is not whole 32-bit words: " + path);
    }

    std::vector<uint32_t> words(fileSize / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), fileSize);
    return words;
}

} // namespace kke
