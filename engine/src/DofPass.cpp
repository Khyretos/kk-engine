#include "DofPass.h"

#include "kke/Buffer.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <array>
#include <cmath>
#include <string>

namespace kke {

namespace {

struct DofPush {
    glm::mat4 invProj;
    glm::vec4 lens;  // focal length (mm), f-number, image height (pixels), largest radius (pixels)
    glm::vec4 focus; // fixed focus (m, 0 = auto), follow speed, seconds since last frame, gather taps
};
static_assert(sizeof(DofPush) <= 128, "push constants: the guaranteed minimum is 128 bytes");

constexpr uint32_t kBindings = 8;
VkDescriptorType bindingType(uint32_t i) {
    if (i == 0) return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    if (i == 3 || i == 5 || i == 7) return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

} // namespace

DofPass::DofPass(VulkanDevice& device) : m_device(device) {
    VkDevice dev = device.device();
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_nearest));
    si.magFilter = si.minFilter = VK_FILTER_LINEAR; // the blur, read between its half-size pixels
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_linear));

    std::array<VkDescriptorSetLayoutBinding, kBindings> b{};
    for (uint32_t i = 0; i < kBindings; ++i) b[i] = { i, bindingType(i), 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings = b.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout));

    VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DofPush) };
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &m_layout));

    constexpr uint32_t n = Renderer::kMaxFramesInFlight;
    const std::array<VkDescriptorPoolSize, 3> sizes{ { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, n },
                                                       { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 * n },
                                                       { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * n } } };
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

    m_focus = std::make_unique<Buffer>(device, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
}

DofPass::~DofPass() {
    VkDevice dev = m_device.device();
    vkDeviceWaitIdle(dev);
    destroyImages();
    m_focus.reset();
    for (auto& pair : m_pipelines)
        for (VkPipeline p : pair)
            if (p) vkDestroyPipeline(dev, p, nullptr);
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
    if (m_layout) vkDestroyPipelineLayout(dev, m_layout, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(dev, m_setLayout, nullptr);
    if (m_nearest) vkDestroySampler(dev, m_nearest, nullptr);
    if (m_linear) vkDestroySampler(dev, m_linear, nullptr);
}

VkPipeline DofPass::pipeline(Step step, bool multisampled) {
    if (step == Gather) multisampled = false; // reads no depth
    VkPipeline& p = m_pipelines[step][multisampled ? 1 : 0];
    if (p) return p;
    static const char* const kNames[StepCount] = { "focus", "half", "gather", "composite" };
    const std::string path = std::string("shaders/dof_") + kNames[step] + (multisampled ? "_ms" : "") + ".comp.spv";
    VkShaderModule comp = loadShaderModule(m_device, path.c_str());
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

void DofPass::destroyImages() {
    VkDevice dev = m_device.device();
    for (Image* i : { &m_half, &m_blurred, &m_result }) {
        if (i->view) vkDestroyImageView(dev, i->view, nullptr);
        if (i->image) vmaDestroyImage(m_device.allocator(), i->image, i->alloc);
        *i = Image{};
    }
}

void DofPass::makeImage(Image& out, VkExtent2D extent) {
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

void DofPass::ensureImages(VkExtent2D extent) {
    if (m_result.image && extent.width == m_extent.width && extent.height == m_extent.height) return;
    // A new size: the other frame in flight may still read the old images.
    vkDeviceWaitIdle(m_device.device());
    destroyImages();
    m_extent = extent;
    const VkExtent2D half{ (extent.width + 1) / 2, (extent.height + 1) / 2 };
    makeImage(m_half, half);
    makeImage(m_blurred, half);
    makeImage(m_result, extent);
    m_fresh = true;
}

void DofPass::record(VkCommandBuffer cmd, uint32_t frame, VkImageView scene, VkImageLayout sceneLayout, VkImageView depth, uint32_t depthSamples,
                     VkExtent2D extent, const glm::mat4& proj, float dt) {
    ensureImages(extent);
    const VkDescriptorSet set = m_sets[frame % Renderer::kMaxFramesInFlight];
    const VkDescriptorBufferInfo focusInfo{ m_focus->handle(), 0, VK_WHOLE_SIZE };
    const std::array<VkDescriptorImageInfo, kBindings> info{ { {},
                                                               { m_nearest, scene, sceneLayout },
                                                               { m_nearest, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
                                                               { VK_NULL_HANDLE, m_half.view, VK_IMAGE_LAYOUT_GENERAL },
                                                               { m_nearest, m_half.view, VK_IMAGE_LAYOUT_GENERAL },
                                                               { VK_NULL_HANDLE, m_blurred.view, VK_IMAGE_LAYOUT_GENERAL },
                                                               { m_linear, m_blurred.view, VK_IMAGE_LAYOUT_GENERAL },
                                                               { VK_NULL_HANDLE, m_result.view, VK_IMAGE_LAYOUT_GENERAL } } };
    std::array<VkWriteDescriptorSet, kBindings> w{};
    for (uint32_t i = 0; i < kBindings; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = bindingType(i);
        if (i == 0) w[i].pBufferInfo = &focusInfo;
        else w[i].pImageInfo = &info[i];
    }
    vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

    if (m_focusFresh) {
        // No focus yet: the first frame takes the target at once.
        vkCmdFillBuffer(cmd, m_focus->handle(), 0, VK_WHOLE_SIZE, 0);
        VkBufferMemoryBarrier fb{};
        fb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        fb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        fb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        fb.srcQueueFamilyIndex = fb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        fb.buffer = m_focus->handle();
        fb.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &fb, 0, nullptr);
        m_focusFresh = false;
    }

    // The scene may come straight from a compute pass (SMAA, TAA) whose barrier reached only the fragment stage;
    // last frame's bloom and tone map are done with the images written now.
    VkMemoryBarrier source{};
    source.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    source.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    source.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    std::array<VkImageMemoryBarrier, 3> before{};
    const std::array<VkImage, 3> images{ m_half.image, m_blurred.image, m_result.image };
    for (size_t i = 0; i < before.size(); ++i) {
        before[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        before[i].srcAccessMask = 0;
        before[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        before[i].oldLayout = m_fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
        before[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        before[i].srcQueueFamilyIndex = before[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        before[i].image = images[i];
        before[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    }
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &source, 0, nullptr, static_cast<uint32_t>(before.size()), before.data());
    m_fresh = false;

    const float fovY = 2.0f * std::atan(1.0f / std::abs(proj[1][1]));
    const float height = static_cast<float>(extent.height);
    DofPush push{};
    push.invProj = glm::inverse(proj);
    push.lens = glm::vec4(m_settings.focalLength > 0.0f ? m_settings.focalLength : dofFocalLength(fovY), m_settings.fStop, height,
                          m_settings.maxBlur * height);
    push.focus = glm::vec4(m_settings.focusDistance, m_settings.focusSpeed, dt, static_cast<float>(kTaps));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);

    // Each step's writes are read by the next.
    VkMemoryBarrier between{};
    between.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    between.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    between.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    auto next = [&] {
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &between, 0, nullptr, 0, nullptr);
    };
    const bool ms = depthSamples > 1;
    const uint32_t hx = (extent.width + 1) / 2, hy = (extent.height + 1) / 2;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline(Focus, ms));
    vkCmdDispatch(cmd, 1, 1, 1);
    next();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline(Half, ms));
    vkCmdDispatch(cmd, (hx + 7) / 8, (hy + 7) / 8, 1);
    next();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline(Gather, ms));
    vkCmdDispatch(cmd, (hx + 7) / 8, (hy + 7) / 8, 1);
    next();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline(Composite, ms));
    vkCmdDispatch(cmd, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);

    // Written before bloom (compute) and the tone map (fragment) read it.
    VkImageMemoryBarrier done = before[2];
    done.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    done.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    done.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &done);
}

} // namespace kke
