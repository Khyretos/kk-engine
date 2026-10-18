#include "DepthMapPass.h"

#include "kke/Buffer.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <array>

namespace kke {

DepthMapPass::DepthMapPass(VulkanDevice& device) : m_device(device) {
    VkDevice dev = device.device();
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_nearest));

    std::array<VkDescriptorSetLayoutBinding, 2> b{};
    b[0] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    b[1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings = b.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout));

    VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::uvec4) };
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &m_layout));

    constexpr uint32_t n = Renderer::kMaxFramesInFlight;
    const std::array<VkDescriptorPoolSize, 2> sizes{ { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, n }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, n } } };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = n;
    pi.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pi.pPoolSizes = sizes.data();
    VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_pool));
    std::array<VkDescriptorSetLayout, n> layouts;
    layouts.fill(m_setLayout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_pool;
    ai.descriptorSetCount = n;
    ai.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(dev, &ai, m_sets));
}

DepthMapPass::~DepthMapPass() {
    VkDevice dev = m_device.device();
    vkDeviceWaitIdle(dev);
    for (Slot& s : m_slots) s.buffer.reset();
    for (VkPipeline p : m_pipelines)
        if (p) vkDestroyPipeline(dev, p, nullptr);
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
    if (m_layout) vkDestroyPipelineLayout(dev, m_layout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(dev, m_setLayout, nullptr);
    if (m_nearest) vkDestroySampler(dev, m_nearest, nullptr);
}

VkPipeline DepthMapPass::pipeline(bool multisampled) {
    VkPipeline& p = m_pipelines[multisampled ? 1 : 0];
    if (p) return p;
    VkShaderModule comp = loadShaderModule(m_device, multisampled ? "shaders/depth_map_ms.comp.spv" : "shaders/depth_map.comp.spv");
    VkComputePipelineCreateInfo cp{};
    cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cp.stage.module = comp;
    cp.stage.pName = "main";
    cp.layout = m_layout;
    VK_CHECK(vkCreateComputePipelines(m_device.device(), VK_NULL_HANDLE, 1, &cp, nullptr, &p));
    vkDestroyShaderModule(m_device.device(), comp, nullptr);
    return p;
}

void DepthMapPass::record(VkCommandBuffer cmd, uint32_t frame, VkImageView depth, uint32_t samples, VkExtent2D extent, const glm::mat4& viewProj) {
    Slot& s = m_slots[frame % Renderer::kMaxFramesInFlight];
    const glm::uvec2 size = depthMapSize(extent.width, extent.height);
    const size_t cells = static_cast<size_t>(size.x) * size.y;
    if (s.capacity < cells) {
        // This slot's last frame is finished (its fence): its old buffer can go now.
        s.buffer = std::make_unique<Buffer>(m_device, cells * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
        s.capacity = cells;
    }
    s.size = size;
    s.viewProj = viewProj;
    s.written = true;
    const VkDescriptorSet set = m_sets[frame % Renderer::kMaxFramesInFlight];
    const VkDescriptorImageInfo image{ m_nearest, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    const VkDescriptorBufferInfo buffer{ s.buffer->handle(), 0, VK_WHOLE_SIZE };
    std::array<VkWriteDescriptorSet, 2> w{};
    for (uint32_t i = 0; i < 2; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &image;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &buffer;
    vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    // This slot's last map (two frames ago) was written by the same step: the CPU reading it does not order the GPU's writes.
    VkMemoryBarrier again{};
    again.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    again.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    again.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &again, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline(samples > 1));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &set, 0, nullptr);
    const glm::uvec4 push(size, 0u, 0u);
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (size.x + 7) / 8, (size.y + 7) / 8, 1);
    // Written before the CPU reads it, once this frame's fence is waited for.
    VkMemoryBarrier toHost{};
    toHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    toHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0, nullptr, 0, nullptr);
}

const DepthMap* DepthMapPass::read(uint32_t frame) {
    const Slot& s = m_slots[frame % Renderer::kMaxFramesInFlight];
    if (!s.written || !s.buffer) return nullptr;
    m_map.width = s.size.x;
    m_map.height = s.size.y;
    m_map.viewProj = s.viewProj;
    m_map.farthest.resize(static_cast<size_t>(s.size.x) * s.size.y);
    s.buffer->download(m_map.farthest.data(), m_map.farthest.size() * sizeof(float));
    return &m_map;
}

void DepthMapPass::forget() {
    for (Slot& s : m_slots) s.written = false;
}

} // namespace kke
