#include "kke/rhi/VulkanDevice.h"

#include <stdexcept>

namespace kke::rhi {

// R1h stubs: the Coder's run replaces this file.
VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {}
std::unique_ptr<GraphicsPipeline> VulkanGpuDevice::createGraphicsPipeline(const GraphicsPipelineDesc&) { throw std::logic_error("R1h: createGraphicsPipeline"); }

} // namespace kke::rhi
