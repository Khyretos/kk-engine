#include "TaaPass.h"

#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <array>

namespace kke {

namespace {

struct TaaPush {
    glm::mat4 reproject;
    glm::vec4 params; // the new frame's share, clip spread, 1 = there is a history
};
static_assert(sizeof(TaaPush) <= 128, "push constants: the guaranteed minimum is 128 bytes");

} // namespace

TaaPass::TaaPass(VulkanDevice& device) : m_device(device) {
    VkDevice dev = device.device();
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_nearest));
    si.magFilter = si.minFilter = VK_FILTER_LINEAR; // the history, read between pixels where it moved
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_linear));

    std::array<VkDescriptorSetLayoutBinding, 4> b{};
    for (uint32_t i = 0; i < 4; ++i)
        b[i] = { i, i < 3 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings = b.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout));

    VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TaaPush) };
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &m_layout));

    constexpr uint32_t n = Renderer::kMaxFramesInFlight;
    const std::array<VkDescriptorPoolSize, 2> sizes{ { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * n }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, n } } };
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

TaaPass::~TaaPass() {
    VkDevice dev = m_device.device();
    vkDeviceWaitIdle(dev);
    destroyImages();
    for (VkPipeline p : m_pipelines)
        if (p) vkDestroyPipeline(dev, p, nullptr);
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
    if (m_layout) vkDestroyPipelineLayout(dev, m_layout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(dev, m_setLayout, nullptr);
    if (m_nearest) vkDestroySampler(dev, m_nearest, nullptr);
    if (m_linear) vkDestroySampler(dev, m_linear, nullptr);
}

VkPipeline TaaPass::pipeline(bool multisampled) {
    VkPipeline& p = m_pipelines[multisampled ? 1 : 0];
    if (p) return p;
    VkShaderModule comp = loadShaderModule(m_device, multisampled ? "shaders/taa_ms.comp.spv" : "shaders/taa.comp.spv");
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

void TaaPass::destroyImages() {
    VkDevice dev = m_device.device();
    for (Image& i : m_history) {
        if (i.view) vkDestroyImageView(dev, i.view, nullptr);
        if (i.image) vmaDestroyImage(m_device.allocator(), i.image, i.alloc);
        i = Image{};
    }
}

bool TaaPass::ensureImages(VkExtent2D extent) {
    if (m_history[0].image && extent.width == m_extent.width && extent.height == m_extent.height) return false;
    // A new size: the other frame in flight may still read the old images.
    vkDeviceWaitIdle(m_device.device());
    destroyImages();
    m_extent = extent;
    for (Image& out : m_history) {
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
        VK_CHECK(vmaCreateImage(m_device.allocator(), &ii, &ai, &out.image, &out.alloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = out.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ii.format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(m_device.device(), &vi, nullptr, &out.view));
    }
    m_fresh = true;
    return true;
}

void TaaPass::record(VkCommandBuffer cmd, uint32_t frame, VkImageView current, VkImageLayout currentLayout, VkImageView depth, uint32_t depthSamples,
                     VkExtent2D extent, const glm::mat4& reproject, bool historyValid) {
    if (ensureImages(extent)) historyValid = false;
    const uint32_t read = m_written, write = 1 - m_written;
    const VkDescriptorSet set = m_sets[frame % Renderer::kMaxFramesInFlight];
    const std::array<VkDescriptorImageInfo, 4> info{ { { m_nearest, current, currentLayout },
                                                       { m_nearest, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
                                                       { m_linear, m_history[read].view, VK_IMAGE_LAYOUT_GENERAL },
                                                       { VK_NULL_HANDLE, m_history[write].view, VK_IMAGE_LAYOUT_GENERAL } } };
    std::array<VkWriteDescriptorSet, 4> w{};
    for (uint32_t i = 0; i < 4; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = i < 3 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[i].pImageInfo = &info[i];
    }
    vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

    // New images get their layout; the image written now was last read as history (or by the tone map) a frame ago.
    std::array<VkImageMemoryBarrier, 2> b{};
    for (uint32_t i = 0; i < 2; ++i) {
        b[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b[i].srcAccessMask = 0;
        b[i].dstAccessMask = i == write ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
        b[i].oldLayout = m_fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
        b[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].image = m_history[i].image;
        b[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, static_cast<uint32_t>(b.size()), b.data());
    if (m_fresh) historyValid = false; // nothing written yet: the history is undefined
    m_fresh = false;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline(depthSamples > 1));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &set, 0, nullptr);
    const TaaPush push{ reproject, glm::vec4(m_settings.blend, m_settings.clipSpread, historyValid ? 1.0f : 0.0f, 0.0f) };
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
    m_written = write;
    // Written before the tone map reads it (and the next frame's step as history).
    VkImageMemoryBarrier done = b[write];
    done.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    done.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    done.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &done);
}

} // namespace kke
