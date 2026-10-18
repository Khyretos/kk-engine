#include "ClusterLightPass.h"

#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <algorithm>
#include <array>

namespace kke {

namespace {

struct ClusterPush {
    glm::mat4 view;
    glm::vec4 proj;  // proj[0][0], proj[1][1], near, far
    glm::uvec4 grid; // cells across, down, deep; lights a cell lists at most
    glm::uvec4 count; // how many lights
};
static_assert(sizeof(ClusterPush) <= 128, "push constants: the guaranteed minimum is 128 bytes");

} // namespace

ClusterLightPass::ClusterLightPass(VulkanDevice& device) : m_device(device) {
    VkDevice dev = device.device();
    std::array<VkDescriptorSetLayoutBinding, 3> b{};
    for (uint32_t i = 0; i < 3; ++i) b[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings = b.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout));
    VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ClusterPush) };
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &m_layout));
    VkShaderModule comp = loadShaderModule(m_device, "shaders/cluster_lights.comp.spv");
    VkComputePipelineCreateInfo cp{};
    cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cp.stage.module = comp;
    cp.stage.pName = "main";
    cp.layout = m_layout;
    VK_CHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cp, nullptr, &m_pipeline));
    vkDestroyShaderModule(dev, comp, nullptr);

    constexpr uint32_t n = Renderer::kMaxFramesInFlight;
    const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * n };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = n;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_pool));
    std::array<VkDescriptorSetLayout, n> layouts;
    layouts.fill(m_setLayout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_pool;
    ai.descriptorSetCount = n;
    ai.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(dev, &ai, m_sets));

    const VkDeviceSize cells = static_cast<VkDeviceSize>(m_grid.x) * m_grid.y * m_grid.z;
    for (uint32_t s = 0; s < n; ++s) {
        Slot& slot = m_slots[s];
        slot.lights = std::make_unique<Buffer>(m_device, kMaxLights * sizeof(GpuLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        slot.counts = std::make_unique<Buffer>(m_device, cells * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        slot.indices = std::make_unique<Buffer>(m_device, cells * m_grid.maxPerCluster * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_GPU_ONLY);
        const std::array<VkDescriptorBufferInfo, 3> info{ { { slot.lights->handle(), 0, VK_WHOLE_SIZE },
                                                            { slot.counts->handle(), 0, VK_WHOLE_SIZE },
                                                            { slot.indices->handle(), 0, VK_WHOLE_SIZE } } };
        std::array<VkWriteDescriptorSet, 3> w{};
        for (uint32_t i = 0; i < 3; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = m_sets[s];
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[i].pBufferInfo = &info[i];
        }
        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    }
}

ClusterLightPass::~ClusterLightPass() {
    VkDevice dev = m_device.device();
    vkDeviceWaitIdle(dev);
    if (m_pipeline) vkDestroyPipeline(dev, m_pipeline, nullptr);
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
    if (m_layout) vkDestroyPipelineLayout(dev, m_layout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(dev, m_setLayout, nullptr);
}

void ClusterLightPass::upload(uint32_t frame, const std::vector<GpuLight>& lights) {
    // This slot's previous frame has finished (Renderer::beginFrame waited on it).
    Slot& slot = m_slots[frame % Renderer::kMaxFramesInFlight];
    slot.count = static_cast<uint32_t>(std::min<size_t>(lights.size(), kMaxLights));
    if (slot.count > 0) slot.lights->upload(lights.data(), slot.count * sizeof(GpuLight));
}

void ClusterLightPass::record(VkCommandBuffer cmd, uint32_t frame, const glm::mat4& view, const glm::mat4& proj) {
    const uint32_t s = frame % Renderer::kMaxFramesInFlight;
    // Before writing: the last frame that used this slot's lists is done with them (beginFrame's fence),
    // and the other slot's reads don't touch these buffers.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &m_sets[s], 0, nullptr);
    const ClusterPush push{ view, glm::vec4(proj[0][0], proj[1][1], m_grid.nearDistance, m_grid.farDistance),
                            glm::uvec4(m_grid.x, m_grid.y, m_grid.z, m_grid.maxPerCluster), glm::uvec4(m_slots[s].count, 0u, 0u, 0u) };
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    const uint32_t cells = m_grid.x * m_grid.y * m_grid.z;
    vkCmdDispatch(cmd, (cells + 63) / 64, 1, 1);
    // Listed before this frame's lit shaders read them.
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

} // namespace kke
