#include "kke/rhi/VulkanDevice.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <stdexcept>

namespace kke::rhi {

VulkanShaderModule::VulkanShaderModule(VkDevice device, std::span<const uint32_t> spirv) : m_device(device) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = static_cast<uint32_t>(spirv.size() * 4);
    createInfo.pCode = spirv.data();

    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &m_module));
}

VulkanShaderModule::~VulkanShaderModule() {
    if (m_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device, m_module, nullptr);
    }
}

std::unique_ptr<ShaderModule> VulkanGpuDevice::createShaderModule(std::span<const uint32_t> spirv) {
    return std::make_unique<VulkanShaderModule>(m_device.device(), spirv);
}

} // namespace kke::rhi
