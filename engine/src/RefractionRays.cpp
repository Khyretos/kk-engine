#include "RefractionRays.h"

#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <algorithm>
#include <array>

namespace kke {

namespace {

// shaders/ray_trace.glsl's Rays block.
struct RayParams {
    glm::mat4 viewToWorld;
    glm::vec4 sunDirection; // w: the ray budget (none here)
    glm::vec4 sunColor;
    glm::vec4 ambient;      // w = 1: classic
};

} // namespace

RefractionRays::RefractionRays(VulkanDevice& device) : m_device(device) {
    VkDevice dev = device.device();
    const std::array<VkDescriptorType, 3> types{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER };
    std::array<VkDescriptorSetLayoutBinding, 3> b{};
    for (uint32_t i = 0; i < 3; ++i) b[i] = { i, types[i], 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = static_cast<uint32_t>(b.size());
    li.pBindings = b.data();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout));

    constexpr uint32_t n = Renderer::kMaxFramesInFlight;
    const std::array<VkDescriptorPoolSize, 3> sizes{ { { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, n },
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

RefractionRays::~RefractionRays() {
    VkDevice dev = m_device.device();
    vkDeviceWaitIdle(dev);
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(dev, m_setLayout, nullptr);
}

VkDescriptorSet RefractionRays::update(uint32_t frame, const RayScene& scene, const std::vector<RayMaterial>& materials,
                                       const glm::mat4& viewToWorld, const glm::vec3& sunDirection, const glm::vec3& sunColor,
                                       const glm::vec3& ambient, bool classic) {
    // This slot's previous frame has finished (Renderer::beginFrame waited on it).
    const uint32_t s = frame % Renderer::kMaxFramesInFlight;
    Slot& slot = m_slots[s];
    const size_t count = std::max<size_t>(1, materials.size());
    if (!slot.materials || slot.materialCapacity < count) {
        slot.materialCapacity = std::max<size_t>(64, count * 2);
        slot.materials = std::make_unique<Buffer>(m_device, slot.materialCapacity * sizeof(RayMaterial), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                  VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    if (!materials.empty()) slot.materials->upload(materials.data(), materials.size() * sizeof(RayMaterial));
    const RayParams params{ viewToWorld, glm::vec4(glm::normalize(sunDirection), 0.0f), glm::vec4(sunColor, 0.0f),
                            glm::vec4(ambient, classic ? 1.0f : 0.0f) };
    slot.params->upload(&params, sizeof(params));

    const VkAccelerationStructureKHR tlas = scene.topLevel(s);
    VkWriteDescriptorSetAccelerationStructureKHR asw{};
    asw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asw.accelerationStructureCount = 1;
    asw.pAccelerationStructures = &tlas;
    const VkDescriptorBufferInfo mb{ slot.materials->handle(), 0, VK_WHOLE_SIZE }, pb{ slot.params->handle(), 0, VK_WHOLE_SIZE };
    std::array<VkWriteDescriptorSet, 3> w{};
    for (uint32_t i = 0; i < 3; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = m_sets[s];
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    w[0].pNext = &asw;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &mb;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &pb;
    vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
    return m_sets[s];
}

} // namespace kke
