#include "kke/rhi/VulkanDevice.h"

#include <stdexcept>

namespace kke::rhi {

// R1h stubs: each unit's Coder run replaces its part.
VulkanShaderModule::VulkanShaderModule(VkDevice device, std::span<const uint32_t>) : m_device(device) { throw std::logic_error("R1h: shader module"); }
VulkanShaderModule::~VulkanShaderModule() {}
std::unique_ptr<ShaderModule> VulkanGpuDevice::createShaderModule(std::span<const uint32_t>) { throw std::logic_error("R1h: createShaderModule"); }

} // namespace kke::rhi
