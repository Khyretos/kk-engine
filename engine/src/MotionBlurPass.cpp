#include "MotionBlurPass.h"

#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <array>

namespace kke {

namespace {

struct MotionBlurPush {
    glm::mat4 reproject;
    glm::vec4 lens;   // projection [2][2] and [3][2], shutter, longest smear (pixels)
    glm::vec4 params; // samples, frame number, 1 = the camera has a last frame
};
static_assert(sizeof(MotionBlurPush) <= 128, "push constants: the guaranteed minimum is 128 bytes");

} // namespace

MotionBlurPass::MotionBlurPass(VulkanDevice& device) : m_device(device) {
    VkDevice dev = device.device();
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_nearest));

    std::array<VkDescriptorSetLayoutBinding, 3> b{};
    for (uint32_t i = 0; i < 3; ++i)
        b[i] = { i, i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings = b.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout));

    VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(MotionBlurPush) };
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &m_layout));

    constexpr uint32_t n = Renderer::kMaxFramesInFlight;
    const std::array<VkDescriptorPoolSize, 2> sizes{ { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * n }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, n } } };
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

MotionBlurPass::~MotionBlurPass() {
    VkDevice dev = m_device.device();
    vkDeviceWaitIdle(dev);
    destroyImage();
    for (VkPipeline p : m_pipelines)
        if (p) vkDestroyPipeline(dev, p, nullptr);
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
    if (m_layout) vkDestroyPipelineLayout(dev, m_layout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(dev, m_setLayout, nullptr);
    if (m_nearest) vkDestroySampler(dev, m_nearest, nullptr);
}

VkPipeline MotionBlurPass::pipeline(bool multisampled) {
    VkPipeline& p = m_pipelines[multisampled ? 1 : 0];
    if (p) return p;
    VkShaderModule comp = loadShaderModule(m_device, multisampled ? "shaders/motion_blur_ms.comp.spv" : "shaders/motion_blur.comp.spv");
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

void MotionBlurPass::destroyImage() {
    if (m_result.view) vkDestroyImageView(m_device.device(), m_result.view, nullptr);
    if (m_result.image) vmaDestroyImage(m_device.allocator(), m_result.image, m_result.alloc);
    m_result = Image{};
}

void MotionBlurPass::ensureImage(VkExtent2D extent) {
    if (m_result.image && extent.width == m_extent.width && extent.height == m_extent.height) return;
    // A new size: the other frame in flight may still read the old image.
    vkDeviceWaitIdle(m_device.device());
    destroyImage();
    m_extent = extent;
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.extent = { extent.width, extent.height, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &ii, &ai, &m_result.image, &m_result.alloc, nullptr));
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = m_result.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ii.format;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device.device(), &vi, nullptr, &m_result.view));
    m_fresh = true;
}

void MotionBlurPass::record(VkCommandBuffer cmd, uint32_t frame, VkImageView current, VkImageLayout currentLayout, VkImageView depth,
                            uint32_t depthSamples, VkExtent2D extent, const glm::mat4& reproject, const glm::mat4& proj, bool moved) {
    ensureImage(extent);
    const VkDescriptorSet set = m_sets[frame % Renderer::kMaxFramesInFlight];
    const std::array<VkDescriptorImageInfo, 3> info{ { { m_nearest, current, currentLayout },
                                                       { m_nearest, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
                                                       { VK_NULL_HANDLE, m_result.view, VK_IMAGE_LAYOUT_GENERAL } } };
    std::array<VkWriteDescriptorSet, 3> w{};
    for (uint32_t i = 0; i < 3; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[i].pImageInfo = &info[i];
    }
    vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

    // The light may come straight from a compute pass (SMAA, TAA, depth of field) whose barrier reached only the
    // fragment stage; last frame's bloom and tone map are done with the image written now.
    VkMemoryBarrier source{};
    source.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    source.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    source.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.oldLayout = m_fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = m_result.image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &source, 0, nullptr, 1, &b);
    m_fresh = false;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline(depthSamples > 1));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &set, 0, nullptr);
    MotionBlurPush push{};
    push.reproject = reproject;
    push.lens = glm::vec4(proj[2][2], proj[3][2], m_settings.shutter, m_settings.maxLength * static_cast<float>(extent.width));
    push.params = glm::vec4(static_cast<float>(m_settings.samples), static_cast<float>(m_frameNumber++ % 1024u), moved ? 1.0f : 0.0f, 0.0f);
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
    // Written before bloom (compute) and the tone map (fragment) read it.
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &b);
}

} // namespace kke
