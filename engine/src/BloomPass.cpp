#include "BloomPass.h"

#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <algorithm>
#include <array>

namespace kke {

namespace {

constexpr uint32_t kMaxLevels = 8;

} // namespace

BloomPass::BloomPass(VulkanDevice& device) : m_device(device) {
    VkDevice dev = device.device();
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_linear));

    auto makeLayout = [dev](uint32_t samplers, VkDescriptorSetLayout& out) {
        std::array<VkDescriptorSetLayoutBinding, 3> b{};
        for (uint32_t i = 0; i <= samplers; ++i)
            b[i] = { i, i < samplers ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
                     nullptr };
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = samplers + 1;
        li.pBindings = b.data();
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &out));
    };
    makeLayout(1, m_downLayout);
    makeLayout(2, m_upLayout);
    VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::vec4) };
    auto makePipelineLayout = [dev, &range](VkDescriptorSetLayout set, VkPipelineLayout& out) {
        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &set;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &range;
        VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &out));
    };
    makePipelineLayout(m_downLayout, m_downPipelineLayout);
    makePipelineLayout(m_upLayout, m_upPipelineLayout);
    m_down = makePipeline("shaders/bloom_down.comp.spv", m_downPipelineLayout);
    m_up = makePipeline("shaders/bloom_up.comp.spv", m_upPipelineLayout);

    const uint32_t sets = Renderer::kMaxFramesInFlight + 2 * kMaxLevels;
    const std::array<VkDescriptorPoolSize, 2> sizes{ { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * sets },
                                                      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, sets } } };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = sets;
    pi.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pi.pPoolSizes = sizes.data();
    VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_pool));
    std::array<VkDescriptorSetLayout, Renderer::kMaxFramesInFlight> layouts;
    layouts.fill(m_downLayout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_pool;
    ai.descriptorSetCount = Renderer::kMaxFramesInFlight;
    ai.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(dev, &ai, m_firstDown));
}

BloomPass::~BloomPass() {
    VkDevice dev = m_device.device();
    vkDeviceWaitIdle(dev);
    destroyImages();
    if (m_down) vkDestroyPipeline(dev, m_down, nullptr);
    if (m_up) vkDestroyPipeline(dev, m_up, nullptr);
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
    if (m_downPipelineLayout) vkDestroyPipelineLayout(dev, m_downPipelineLayout, nullptr);
    if (m_upPipelineLayout) vkDestroyPipelineLayout(dev, m_upPipelineLayout, nullptr);
    if (m_downLayout) vkDestroyDescriptorSetLayout(dev, m_downLayout, nullptr);
    if (m_upLayout) vkDestroyDescriptorSetLayout(dev, m_upLayout, nullptr);
    if (m_linear) vkDestroySampler(dev, m_linear, nullptr);
}

VkPipeline BloomPass::makePipeline(const char* shader, VkPipelineLayout layout) {
    VkShaderModule comp = loadShaderModule(m_device, shader);
    VkComputePipelineCreateInfo cp{};
    cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cp.stage.module = comp;
    cp.stage.pName = "main";
    cp.layout = layout;
    VkPipeline p = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(m_device.device(), VK_NULL_HANDLE, 1, &cp, nullptr, &p));
    vkDestroyShaderModule(m_device.device(), comp, nullptr);
    return p;
}

void BloomPass::destroyImages() {
    VkDevice dev = m_device.device();
    for (std::vector<Image>* list : { &m_downImages, &m_upImages })
        for (Image& i : *list) {
            if (i.view) vkDestroyImageView(dev, i.view, nullptr);
            if (i.image) vmaDestroyImage(m_device.allocator(), i.image, i.alloc);
        }
    m_downImages.clear();
    m_upImages.clear();
    std::vector<VkDescriptorSet> sets = m_downSets;
    sets.insert(sets.end(), m_upSets.begin(), m_upSets.end());
    if (!sets.empty()) vkFreeDescriptorSets(dev, m_pool, static_cast<uint32_t>(sets.size()), sets.data());
    m_downSets.clear();
    m_upSets.clear();
}

void BloomPass::ensureImages(VkExtent2D sceneExtent) {
    const uint32_t levels = std::min(bloomLevels(static_cast<int>(sceneExtent.width), static_cast<int>(sceneExtent.height), m_settings.levels), kMaxLevels);
    if (!m_downImages.empty() && sceneExtent.width == m_sceneExtent.width && sceneExtent.height == m_sceneExtent.height && levels == m_levels) return;
    // A new size: the other frame in flight may still read the old images.
    vkDeviceWaitIdle(m_device.device());
    destroyImages();
    m_sceneExtent = sceneExtent;
    m_levels = levels;
    auto make = [&](VkExtent2D e) {
        Image out;
        out.extent = e;
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.extent = { e.width, e.height, 1 };
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
        return out;
    };
    VkExtent2D e = sceneExtent;
    for (uint32_t i = 0; i < levels; ++i) {
        e = { std::max(e.width / 2, 1u), std::max(e.height / 2, 1u) };
        m_downImages.push_back(make(e));
        if (i + 1 < levels) m_upImages.push_back(make(e)); // the last level is its own build-up
    }
    // Descriptor sets: halving i (i >= 1) reads halving i - 1; build-up i reads build-up i + 1 (or the last halving) and halving i.
    auto allocate = [&](VkDescriptorSetLayout layout, std::vector<VkDescriptorSet>& out, uint32_t count) {
        if (count == 0) return;
        std::vector<VkDescriptorSetLayout> layouts(count, layout);
        out.resize(count);
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_pool;
        ai.descriptorSetCount = count;
        ai.pSetLayouts = layouts.data();
        VK_CHECK(vkAllocateDescriptorSets(m_device.device(), &ai, out.data()));
    };
    allocate(m_downLayout, m_downSets, levels > 0 ? levels - 1 : 0);
    allocate(m_upLayout, m_upSets, levels > 0 ? levels - 1 : 0);
    std::vector<VkDescriptorImageInfo> info;
    info.reserve(5 * kMaxLevels);
    std::vector<VkWriteDescriptorSet> w;
    auto add = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorType type, VkImageView view) {
        info.push_back({ type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ? m_linear : VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL });
        VkWriteDescriptorSet x{};
        x.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        x.dstSet = set;
        x.dstBinding = binding;
        x.descriptorCount = 1;
        x.descriptorType = type;
        x.pImageInfo = &info.back();
        w.push_back(x);
    };
    for (uint32_t i = 1; i < levels; ++i) {
        add(m_downSets[i - 1], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_downImages[i - 1].view);
        add(m_downSets[i - 1], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_downImages[i].view);
    }
    for (uint32_t i = 0; i + 1 < levels; ++i) {
        const VkImageView coarse = i + 2 < levels ? m_upImages[i + 1].view : m_downImages[levels - 1].view;
        add(m_upSets[i], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, coarse);
        add(m_upSets[i], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_downImages[i].view);
        add(m_upSets[i], 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_upImages[i].view);
    }
    if (!w.empty()) vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    m_fresh = true;
}

VkImageView BloomPass::output() const {
    if (m_upImages.empty()) return m_downImages.empty() ? VK_NULL_HANDLE : m_downImages[0].view;
    return m_upImages[0].view;
}

void BloomPass::record(VkCommandBuffer cmd, uint32_t frame, VkImageView scene, VkImageLayout sceneLayout, VkExtent2D extent) {
    ensureImages(extent);
    if (m_levels == 0) return;
    const VkDescriptorSet first = m_firstDown[frame % Renderer::kMaxFramesInFlight];
    const std::array<VkDescriptorImageInfo, 2> info{ { { m_linear, scene, sceneLayout }, { VK_NULL_HANDLE, m_downImages[0].view, VK_IMAGE_LAYOUT_GENERAL } } };
    std::array<VkWriteDescriptorSet, 2> w{};
    for (uint32_t i = 0; i < 2; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = first;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[i].pImageInfo = &info[i];
    }
    vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

    auto barrier = [](VkImage image, VkAccessFlags src, VkAccessFlags dst, VkImageLayout from) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask = src;
        b.dstAccessMask = dst;
        b.oldLayout = from;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        return b;
    };
    // Before writing: last frame's chain and tone map are done with every level.
    std::vector<VkImageMemoryBarrier> before;
    for (std::vector<Image>* list : { &m_downImages, &m_upImages })
        for (Image& i : *list) before.push_back(barrier(i.image, 0, VK_ACCESS_SHADER_WRITE_BIT, m_fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL));
    // The source may come straight from a compute pass (SMAA, TAA) whose own barrier only reached the fragment stage.
    VkMemoryBarrier source{};
    source.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    source.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    source.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &source, 0, nullptr, static_cast<uint32_t>(before.size()), before.data());
    m_fresh = false;
    auto written = [&](VkImage image) {
        const VkImageMemoryBarrier b = barrier(image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &b);
    };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_down);
    for (uint32_t i = 0; i < m_levels; ++i) {
        const VkDescriptorSet set = i == 0 ? first : m_downSets[i - 1];
        const glm::vec4 push(i == 0 ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f); // the first halving weighs its boxes
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_downPipelineLayout, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, m_downPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (m_downImages[i].extent.width + 7) / 8, (m_downImages[i].extent.height + 7) / 8, 1);
        written(m_downImages[i].image);
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_up);
    for (int i = static_cast<int>(m_levels) - 2; i >= 0; --i) {
        const glm::vec4 push(i == 0 ? 1.0f / static_cast<float>(m_levels) : 1.0f, 0.0f, 0.0f, 0.0f); // the last step: every level added once
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_upPipelineLayout, 0, 1, &m_upSets[static_cast<size_t>(i)], 0, nullptr);
        vkCmdPushConstants(cmd, m_upPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (m_upImages[static_cast<size_t>(i)].extent.width + 7) / 8, (m_upImages[static_cast<size_t>(i)].extent.height + 7) / 8, 1);
        written(m_upImages[static_cast<size_t>(i)].image);
    }
}

} // namespace kke
