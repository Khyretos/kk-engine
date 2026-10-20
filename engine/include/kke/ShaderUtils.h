#pragma once

#include <volk.h>
#include <cstdint>
#include <string>
#include <vector>

namespace kke {

class VulkanDevice;

// Loads a compiled .spv file into a VkShaderModule. Shared by Pipeline
// (graphics) and anything that builds a compute pipeline directly (e.g.
// ParticleModule) since compute pipelines don't go through Pipeline.
VkShaderModule loadShaderModule(VulkanDevice& device, const std::string& path);

// The 32-bit words of a compiled .spv file, for rhi::Device::createShaderModule (R1h).
// Throws std::runtime_error when the file cannot be opened or its size is not a multiple of 4.
std::vector<uint32_t> loadSpirv(const std::string& path);

} // namespace kke
