#include "VolumetricFogPass.h"

#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <array>
#include <vector>

namespace kke {

namespace {

struct FogPush {
    glm::mat4 invViewProj;
    glm::vec4 grid; // near distance, far distance, slices
};
static_assert(sizeof(FogPush) <= 128, "push constants: the guaranteed minimum is 128 bytes");

} // namespace

VolumetricFogPass::VolumetricFogPass(VulkanDevice& device, VkDescriptorSetLayout lightingLayout, VkDescriptorSetLayout shadowLayout)
    : m_device(device) {
    VkDevice dev = device.device();
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_linear));

    auto makeLayout = [dev](uint32_t count, VkDescriptorSetLayout& out) {
        std::array<VkDescriptorSetLayoutBinding, 2> b{};
        for (uint32_t i = 0; i < count; ++i) b[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = count;
        li.pBindings = b.data();
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &out));
    };
    makeLayout(1, m_cellLayout);
    makeLayout(2, m_integrateLayout);

    VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FogPush) };
    auto makePipelineLayout = [dev, &range](const std::vector<VkDescriptorSetLayout>& sets, VkPipelineLayout& out) {
        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = static_cast<uint32_t>(sets.size());
        pl.pSetLayouts = sets.data();
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &range;
        VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &out));
    };
    makePipelineLayout({ lightingLayout, shadowLayout, m_cellLayout }, m_scatterPipelineLayout);
    makePipelineLayout({ m_integrateLayout }, m_integratePipelineLayout);
    m_scatter = makePipeline("shaders/volumetric_scatter.comp.spv", m_scatterPipelineLayout);
    m_integrate = makePipeline("shaders/volumetric_integrate.comp.spv", m_integratePipelineLayout);

    const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 2;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_pool));
    const std::array<VkDescriptorSetLayout, 2> layouts{ m_cellLayout, m_integrateLayout };
    std::array<VkDescriptorSet, 2> sets{};
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_pool;
    ai.descriptorSetCount = 2;
    ai.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(dev, &ai, sets.data()));
    m_cellSet = sets[0];
    m_integrateSet = sets[1];
}

VolumetricFogPass::~VolumetricFogPass() {
    VkDevice dev = m_device.device();
    vkDeviceWaitIdle(dev);
    destroyImages();
    if (m_scatter) vkDestroyPipeline(dev, m_scatter, nullptr);
    if (m_integrate) vkDestroyPipeline(dev, m_integrate, nullptr);
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
    if (m_scatterPipelineLayout) vkDestroyPipelineLayout(dev, m_scatterPipelineLayout, nullptr);
    if (m_integratePipelineLayout) vkDestroyPipelineLayout(dev, m_integratePipelineLayout, nullptr);
    if (m_cellLayout) vkDestroyDescriptorSetLayout(dev, m_cellLayout, nullptr);
    if (m_integrateLayout) vkDestroyDescriptorSetLayout(dev, m_integrateLayout, nullptr);
    if (m_linear) vkDestroySampler(dev, m_linear, nullptr);
}

VkPipeline VolumetricFogPass::makePipeline(const char* shader, VkPipelineLayout layout) {
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

void VolumetricFogPass::destroyImages() {
    VkDevice dev = m_device.device();
    for (Image* i : { &m_cells, &m_volume }) {
        if (i->view) vkDestroyImageView(dev, i->view, nullptr);
        if (i->image) vmaDestroyImage(m_device.allocator(), i->image, i->alloc);
        *i = Image{};
    }
}

void VolumetricFogPass::prepare(const VolumetricFogSettings& settings) {
    if (m_cells.image && settings.width == m_settings.width && settings.height == m_settings.height && settings.slices == m_settings.slices) {
        m_settings = settings; // distances only: same images
        return;
    }
    // A new size: the frame in flight may still read the old images.
    vkDeviceWaitIdle(m_device.device());
    destroyImages();
    m_settings = settings;
    auto make = [&](VkImageUsageFlags usage, Image& out) {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_3D;
        ii.extent = { settings.width, settings.height, settings.slices };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage = usage;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        VK_CHECK(vmaCreateImage(m_device.allocator(), &ii, &ai, &out.image, &out.alloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = out.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
        vi.format = ii.format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(m_device.device(), &vi, nullptr, &out.view));
    };
    make(VK_IMAGE_USAGE_STORAGE_BIT, m_cells);
    make(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_volume);
    const std::array<VkDescriptorImageInfo, 2> info{ { { VK_NULL_HANDLE, m_cells.view, VK_IMAGE_LAYOUT_GENERAL },
                                                       { VK_NULL_HANDLE, m_volume.view, VK_IMAGE_LAYOUT_GENERAL } } };
    std::array<VkWriteDescriptorSet, 3> w{};
    for (uint32_t i = 0; i < 3; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].descriptorCount = 1;
        w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    w[0].dstSet = m_cellSet;
    w[0].dstBinding = 0;
    w[0].pImageInfo = &info[0];
    w[1].dstSet = m_integrateSet;
    w[1].dstBinding = 0;
    w[1].pImageInfo = &info[0];
    w[2].dstSet = m_integrateSet;
    w[2].dstBinding = 1;
    w[2].pImageInfo = &info[1];
    vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    m_fresh = true;
}

void VolumetricFogPass::record(VkCommandBuffer cmd, VkDescriptorSet lighting, VkDescriptorSet shadow, const glm::mat4& invViewProj) {
    if (!m_cells.image) return;
    auto barrier = [&](VkImage image, VkAccessFlags src, VkAccessFlags dst, VkImageLayout from) {
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
    const VkImageLayout from = m_fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
    // Before writing: the last frame's integrate step and lit shaders are done with them, and this
    // frame's shadow map is written (the shadow pass's depth writes, read by the scatter step).
    const std::array<VkImageMemoryBarrier, 2> before{ barrier(m_cells.image, 0, VK_ACCESS_SHADER_WRITE_BIT, from),
                                                      barrier(m_volume.image, 0, VK_ACCESS_SHADER_WRITE_BIT, from) };
    VkMemoryBarrier depth{};
    depth.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    depth.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depth.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &depth, 0, nullptr, static_cast<uint32_t>(before.size()), before.data());
    m_fresh = false;

    const FogPush push{ invViewProj, glm::vec4(m_settings.nearDistance, m_settings.farDistance, static_cast<float>(m_settings.slices), 0.0f) };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scatter);
    const std::array<VkDescriptorSet, 3> sets{ lighting, shadow, m_cellSet };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_scatterPipelineLayout, 0, 3, sets.data(), 0, nullptr);
    vkCmdPushConstants(cmd, m_scatterPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (m_settings.width + 7) / 8, (m_settings.height + 7) / 8, m_settings.slices);

    const VkImageMemoryBarrier cellsDone = barrier(m_cells.image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cellsDone);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_integrate);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_integratePipelineLayout, 0, 1, &m_integrateSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_integratePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (m_settings.width + 7) / 8, (m_settings.height + 7) / 8, 1);

    // Written before this frame's lit shaders (and the sky) read it.
    const VkImageMemoryBarrier volumeDone = barrier(m_volume.image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &volumeDone);
}

} // namespace kke
