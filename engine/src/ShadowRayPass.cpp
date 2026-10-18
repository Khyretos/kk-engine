#include "ShadowRayPass.h"

#include "ReflectionPass.h"
#include "kke/RayScene.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <algorithm>
#include <array>

namespace kke {

namespace {

struct ShadowRayPush {
    glm::mat4 invProj;
    glm::vec4 limits; // size in pixels, KKE's nearest traced distance, the farthest
};
static_assert(sizeof(ShadowRayPush) <= 128, "push constants: the guaranteed minimum is 128 bytes");

// shaders/ray_trace.glsl's Rays block.
struct RayParams {
    glm::mat4 viewToWorld;
    glm::vec4 sunDirection; // w: the ray budget (none here)
    glm::vec4 sunColor;
    glm::vec4 ambient; // w = 1: classic
};

} // namespace

ShadowRayPass::ShadowRayPass(VulkanDevice& device) : m_device(device) {
    VkDevice dev = device.device();
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_nearest));

    const std::array<VkDescriptorType, 5> types{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                 VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER };
    std::array<VkDescriptorSetLayoutBinding, 5> b{};
    for (uint32_t i = 0; i < 5; ++i) b[i] = { i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings = b.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout));

    VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ShadowRayPush) };
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &m_setLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &m_layout));

    constexpr uint32_t n = Renderer::kMaxFramesInFlight;
    const std::array<VkDescriptorPoolSize, 5> sizes{ { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, n },
                                                      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, n },
                                                      { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, n },
                                                      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, n },
                                                      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, n } } };
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
    for (Slot& slot : m_slots)
        slot.params = std::make_unique<Buffer>(m_device, sizeof(RayParams), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
}

ShadowRayPass::~ShadowRayPass() {
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

VkPipeline ShadowRayPass::pipeline(bool multisampled) {
    VkPipeline& p = m_pipelines[multisampled ? 1 : 0];
    if (p) return p;
    VkShaderModule comp = loadShaderModule(m_device, multisampled ? "shaders/shadow_rays_ms.comp.spv" : "shaders/shadow_rays.comp.spv");
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

void ShadowRayPass::destroyImage() {
    VkDevice dev = m_device.device();
    if (m_image.view) vkDestroyImageView(dev, m_image.view, nullptr);
    if (m_image.image) vmaDestroyImage(m_device.allocator(), m_image.image, m_image.alloc);
    m_image = Image{};
}

void ShadowRayPass::prepare(VkExtent2D sceneExtent) {
    if (m_image.image && sceneExtent.width == m_extent.width && sceneExtent.height == m_extent.height) return;
    // A resize: the frame in flight may still read the old image.
    vkDeviceWaitIdle(m_device.device());
    destroyImage();
    m_extent = sceneExtent;
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.extent = { sceneExtent.width, sceneExtent.height, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.format = VK_FORMAT_R16G16_SFLOAT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &ii, &ai, &m_image.image, &m_image.alloc, nullptr));
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = m_image.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ii.format;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device.device(), &vi, nullptr, &m_image.view));
    m_fresh = true;
    m_ready = false;
}

void ShadowRayPass::record(VkCommandBuffer cmd, uint32_t frame, const Renderer::SceneImages& scene, const glm::mat4& view, const glm::mat4& proj,
                           const ReflectionRays& rays, const ShadowRaySettings& settings) {
    if (!scene.depth || !rays.scene || !rays.materials || !m_image.image || scene.extent.width != m_extent.width ||
        scene.extent.height != m_extent.height) {
        m_ready = false;
        return;
    }
    const uint32_t s = frame % Renderer::kMaxFramesInFlight;
    Slot& slot = m_slots[s];
    const std::vector<RayMaterial>& materials = *rays.materials;
    const size_t count = std::max<size_t>(1, materials.size());
    if (!slot.materials || slot.materialCapacity < count) {
        slot.materialCapacity = std::max<size_t>(64, count * 2);
        slot.materials = std::make_unique<Buffer>(m_device, slot.materialCapacity * sizeof(RayMaterial), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                  VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    if (!materials.empty()) slot.materials->upload(materials.data(), materials.size() * sizeof(RayMaterial));
    const RayParams params{ rays.viewToWorld, glm::vec4(glm::normalize(rays.sunDirection), 0.0f), glm::vec4(rays.sunColor, 0.0f),
                            glm::vec4(rays.ambient, rays.classic ? 1.0f : 0.0f) };
    slot.params->upload(&params, sizeof(params));

    const VkDescriptorSet set = m_sets[s];
    const VkDescriptorImageInfo depth{ m_nearest, scene.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    const VkDescriptorImageInfo out{ VK_NULL_HANDLE, m_image.view, VK_IMAGE_LAYOUT_GENERAL };
    const VkAccelerationStructureKHR tlas = rays.scene->topLevel(s);
    VkWriteDescriptorSetAccelerationStructureKHR asw{};
    asw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asw.accelerationStructureCount = 1;
    asw.pAccelerationStructures = &tlas;
    const VkDescriptorBufferInfo mb{ slot.materials->handle(), 0, VK_WHOLE_SIZE }, pb{ slot.params->handle(), 0, VK_WHOLE_SIZE };
    std::array<VkWriteDescriptorSet, 5> w{};
    for (uint32_t i = 0; i < 5; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &depth;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[1].pImageInfo = &out;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    w[2].pNext = &asw;
    w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[3].pBufferInfo = &mb;
    w[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[4].pBufferInfo = &pb;
    vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);

    // Before writing: the last frame's lit shaders have finished reading it.
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.oldLayout = m_fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = m_image.image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    m_fresh = false;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline(scene.depthSamples > 1));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &set, 0, nullptr);
    const ShadowRayPush push{ glm::inverse(proj),
                              glm::vec4(static_cast<float>(m_extent.width), static_cast<float>(m_extent.height), settings.kkeFrom, settings.maxDistance) };
    vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (m_extent.width + 7) / 8, (m_extent.height + 7) / 8, 1);
    // Written before the next frame's lit shaders read it.
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    m_viewProj = proj * view;
    m_ready = true;
}

} // namespace kke
