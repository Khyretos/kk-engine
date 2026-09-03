#pragma once

#include <volk.h>
#include <string>

namespace kke {

class VulkanDevice;

// Loads a compiled .spv file into a VkShaderModule. Shared by Pipeline
// (graphics) and anything that builds a compute pipeline directly (e.g.
// ParticleModule) since compute pipelines don't go through Pipeline.
VkShaderModule loadShaderModule(VulkanDevice& device, const std::string& path);

} // namespace kke
