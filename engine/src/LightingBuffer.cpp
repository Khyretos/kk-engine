#include "kke/LightingBuffer.h"
#include "kke/Application.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"

#include <glm/glm.hpp>
#include <cstring>

namespace kke {

namespace {

// Mirrors cube.frag's `LightingUBO` GLSL block byte-for-byte. Every
// field is a vec4 deliberately, even where fewer components are
// meaningful -- std140 (the layout Vulkan UBOs use) pads vec3 up to
// vec4-alignment anyway, so using vec4 throughout sidesteps that
// padding rather than fighting it with manual alignas() bookkeeping.
struct GPULight {
    glm::vec4 directionOrPosition; // xyz = direction or position; w = 1.0 if positional (point), 0.0 if directional
    glm::vec4 colorIntensity;      // rgb = color; a = intensity (<=0 means "treat as disabled")
};

struct LightingUBOData {
    GPULight lights[Lighting::kMaxLights];
    glm::vec4 ambient;   // rgb = ambient color; a unused (padding)
    glm::vec4 cameraPos; // rgb = world-space camera position, needed for specular; a unused
    glm::mat4 lightViewProj; // appended -- see ShadowMap.h
    glm::mat4 viewProj; // appended -- the real camera's view*proj, computed once here instead of
                         // redundantly inside every single object's own push constants (see
                         // PBRPushConstants in cube.vert/frag's own comment for why that mattered:
                         // freeing this 64 bytes out of the push constant was what made room for
                         // real per-object metallic/roughness within the 128-byte guaranteed-minimum
                         // push constant limit)
};

} // namespace

LightingBuffer::LightingBuffer(VulkanDevice& device) : m_device(device) {
    // --- Descriptor set layout: one uniform buffer, readable from both
    // vertex and fragment stages (vertex doesn't currently need it, but
    // costs nothing to allow now rather than needing a second layout
    // later for e.g. per-vertex light-space transforms for shadows).
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(device.device(), &layoutInfo, nullptr, &m_setLayout));

    // --- Descriptor pool: exactly one set needed -- every lit module
    // shares this same one light buffer, there's no per-object growth
    // the way RmlVulkanRenderInterface's texture pool needs.
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &m_descriptorPool));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_setLayout;
    VK_CHECK(vkAllocateDescriptorSets(device.device(), &allocInfo, &m_descriptorSet));

    // Host-visible, updated directly every frame (matching the same
    // pattern PhysicsModule already uses for its own per-frame vertex
    // uploads) -- this buffer is tiny (well under 200 bytes for 4
    // lights), so a staging-buffer round trip would be pure overhead
    // for no real benefit.
    m_buffer = std::make_unique<Buffer>(
        device, sizeof(LightingUBOData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_buffer->handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(LightingUBOData);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
}

LightingBuffer::~LightingBuffer() {
    if (m_descriptorPool) vkDestroyDescriptorPool(m_device.device(), m_descriptorPool, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_device.device(), m_setLayout, nullptr);
}

void LightingBuffer::update(const Lighting& lighting, const glm::vec3& cameraPos, const glm::mat4& lightViewProj, const glm::mat4& viewProj) {
    LightingUBOData data{};
    for (int i = 0; i < Lighting::kMaxLights; ++i) {
        const Light& src = lighting.lights[i];
        GPULight& dst = data.lights[i];
        if (!src.enabled) {
            dst.colorIntensity.a = 0.0f; // the shader's own "disabled" convention
            continue;
        }
        if (src.isDirectional) {
            dst.directionOrPosition = glm::vec4(src.direction, 0.0f);
        } else {
            dst.directionOrPosition = glm::vec4(src.position, 1.0f);
        }
        dst.colorIntensity = glm::vec4(src.color, src.intensity);
    }
    data.ambient = glm::vec4(lighting.ambientColor, 0.0f);
    data.cameraPos = glm::vec4(cameraPos, 0.0f);
    data.lightViewProj = lightViewProj;
    data.viewProj = viewProj;

    m_buffer->upload(&data, sizeof(data));
}

} // namespace kke
