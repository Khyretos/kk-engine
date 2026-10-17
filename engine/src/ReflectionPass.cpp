#include "ReflectionPass.h"

#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <array>
#include <cmath>

namespace kke {

namespace {

struct ReflectPush {
    glm::mat4 proj;
    glm::vec4 sizeLimits; // full-resolution size, the smoothest-to-trace limit, max distance
    glm::vec4 march;      // steps, thickness
};
static_assert(sizeof(ReflectPush) <= 128, "push constants: the guaranteed minimum is 128 bytes");

} // namespace

ReflectionPass::ReflectionPass(VulkanDevice& device) : m_device(device) {
    VkDevice dev = device.device();
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_nearest));
    // The lit shaders read the half-resolution result with their own depth-aware weights.
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_linear));

    std::array<VkDescriptorSetLayoutBinding, 4> b{};
    for (uint32_t i = 0; i < 4; ++i) {
        b[i].binding = i;
        b[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings = b.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout));

    VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ReflectPush) };
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &m_layout));

    const std::array<VkDescriptorPoolSize, 2> sizes{ { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * Renderer::kMaxFramesInFlight },
                                                      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 * Renderer::kMaxFramesInFlight } } };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = Renderer::kMaxFramesInFlight;
    pi.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pi.pPoolSizes = sizes.data();
    VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_pool));
    std::array<VkDescriptorSetLayout, Renderer::kMaxFramesInFlight> layouts;
    layouts.fill(m_setLayout);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_pool;
    ai.descriptorSetCount = Renderer::kMaxFramesInFlight;
    ai.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(dev, &ai, m_sets));
}

ReflectionPass::~ReflectionPass() {
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

VkPipeline ReflectionPass::pipeline(bool multisampled) {
    VkPipeline& p = m_pipelines[multisampled ? 1 : 0];
    if (p) return p;
    VkShaderModule comp = loadShaderModule(m_device, multisampled ? "shaders/reflect_ms.comp.spv" : "shaders/reflect.comp.spv");
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

void ReflectionPass::destroyImages() {
    VkDevice dev = m_device.device();
    for (Image* i : { &m_light, &m_depth }) {
        if (i->view) vkDestroyImageView(dev, i->view, nullptr);
        if (i->image) vmaDestroyImage(m_device.allocator(), i->image, i->alloc);
        *i = Image{};
    }
    m_extent = {};
}

void ReflectionPass::prepare(VkExtent2D sceneExtent) { ensureImages(sceneExtent); }

void ReflectionPass::ensureImages(VkExtent2D full) {
    const VkExtent2D half{ (full.width + 1) / 2, (full.height + 1) / 2 };
    if (m_light.image && half.width == m_extent.width && half.height == m_extent.height) return;
    // A resize: the frame in flight may still read the old images.
    vkDeviceWaitIdle(m_device.device());
    destroyImages();
    m_extent = half;
    auto make = [&](VkFormat format, Image& out) {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.extent = { half.width, half.height, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.format = format;
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
        vi.format = format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(m_device.device(), &vi, nullptr, &out.view));
    };
    make(VK_FORMAT_R16G16B16A16_SFLOAT, m_light);
    make(VK_FORMAT_R32_SFLOAT, m_depth);
    m_fresh = true;
    m_ready = false;
}

void ReflectionPass::record(VkCommandBuffer cmd, uint32_t frame, const Renderer::SceneImages& scene, const glm::mat4& view,
                          const glm::mat4& proj, const ReflectionSettings& s) {
    if (!scene.depth || !scene.color || scene.extent.width == 0 || scene.extent.height == 0) {
        m_ready = false;
        return;
    }
    if (!m_light.image || m_extent.width != (scene.extent.width + 1) / 2 || m_extent.height != (scene.extent.height + 1) / 2) {
        m_ready = false; // not prepare()d for this size: next frame
        return;
    }
    const bool ms = scene.depthSamples > 1;
    const VkPipeline p = pipeline(ms);

    // This slot's set: the slot's previous frame has finished (Renderer::beginFrame waited on it).
    const VkDescriptorSet set = m_sets[frame % Renderer::kMaxFramesInFlight];
    std::array<VkDescriptorImageInfo, 4> info{ { { m_nearest, scene.color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                                                 { m_nearest, scene.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
                                                 { VK_NULL_HANDLE, m_light.view, VK_IMAGE_LAYOUT_GENERAL },
                                                 { VK_NULL_HANDLE, m_depth.view, VK_IMAGE_LAYOUT_GENERAL } } };
    std::array<VkWriteDescriptorSet, 4> w{};
    for (uint32_t i = 0; i < 4; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w[i].pImageInfo = &info[i];
    }
    vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

    // Before writing: the last frame's lit shaders have finished reading
    // (or, new images, nothing has: they just get their layout).
    std::array<VkImageMemoryBarrier, 2> b{};
    for (uint32_t i = 0; i < 2; ++i) {
        b[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b[i].srcAccessMask = 0;
        b[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b[i].oldLayout = m_fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
        b[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].image = i == 0 ? m_light.image : m_depth.image;
        b[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(b.size()), b.data());
    m_fresh = false;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &set, 0, nullptr);
    ReflectPush push{};
    push.proj = proj;
    push.sizeLimits = glm::vec4(static_cast<float>(scene.extent.width), static_cast<float>(scene.extent.height), 1.0f - s.maxRoughness,
                                s.maxDistance);
    push.march = glm::vec4(static_cast<float>(s.steps), s.thickness, 0.0f, 0.0f);
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ReflectPush), &push);
    vkCmdDispatch(cmd, (m_extent.width + 7) / 8, (m_extent.height + 7) / 8, 1);

    // Written before the next frame's lit shaders read them.
    for (VkImageMemoryBarrier& x : b) {
        x.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        x.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        x.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<uint32_t>(b.size()), b.data());
    m_viewProj = proj * view;
    m_ready = true;
}

} // namespace kke
