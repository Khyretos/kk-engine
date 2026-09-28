#include "kke/LightingBuffer.h"
#include "kke/Application.h"
#include "kke/Renderer.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstring>

namespace kke {

static_assert(static_cast<int>(LightingBuffer::kSlotCount) == Renderer::kMaxFramesInFlight, "one lighting buffer per frame in flight");

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
    glm::vec4 toneParams; // x = tone mapper (0 AgX, 1 ACES, 2 Reinhard -- shaders/tonemap.glsl), y = exposure
    // The sky, fog and look (kke/Sky.h); shaders/lighting_ubo.glsl names each.
    glm::vec4 lookSlope;
    glm::vec4 lookOffset;
    glm::vec4 lookPower;
    glm::vec4 skyZenith;
    glm::vec4 skyHorizon;
    glm::vec4 skyGround;
    glm::vec4 sunDisc;
    glm::vec4 sunGlow;
    glm::vec4 fogColor;
    glm::vec4 fogParams;
    glm::vec4 ambientSH[9];
    glm::vec4 shadowTile; // this view's tile of the shadow map: x, y (top left), width, height in 0..1
};
static_assert(sizeof(LightingUBOData) == 4 * 2 * 16 + 2 * 16 + 2 * 64 + 11 * 16 + 9 * 16 + 16, "must match shaders/lighting_ubo.glsl");

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

    // --- Descriptor pool: one set per frame in flight -- every lit
    // module shares these, there's no per-object growth the way
    // RmlVulkanRenderInterface's texture pool needs.
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSlotCount };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kSlotCount;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &m_descriptorPool));

    for (uint32_t i = 0; i < kSlotCount; ++i) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_setLayout;
        VK_CHECK(vkAllocateDescriptorSets(device.device(), &allocInfo, &m_descriptorSets[i]));

        // Host-visible, updated directly every frame -- this buffer is
        // tiny, so a staging-buffer round trip would be pure overhead.
        m_buffers[i] = std::make_unique<Buffer>(device, sizeof(LightingUBOData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_buffers[i]->handle();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(LightingUBOData);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_descriptorSets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
    }
}

LightingBuffer::~LightingBuffer() {
    if (m_descriptorPool) vkDestroyDescriptorPool(m_device.device(), m_descriptorPool, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(m_device.device(), m_setLayout, nullptr);
}

void LightingBuffer::update(const Lighting& lighting, const glm::vec3& cameraPos, const glm::mat4& lightViewProj, const glm::mat4& viewProj,
                            const SkyEnvironment* sky, const glm::vec4& shadowTile) {
    LightingUBOData data{};
    data.shadowTile = shadowTile;
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
    data.ambient = glm::vec4(lighting.ambientColor, lighting.shadowsEnabled ? 1.0f : 0.0f); // a = shadows on/off, read by cube.frag
    data.cameraPos = glm::vec4(cameraPos, 0.0f);
    data.lightViewProj = lightViewProj;
    data.viewProj = viewProj;
    data.toneParams = glm::vec4(static_cast<float>(lighting.toneMapper), lighting.exposure, 0.0f, 0.0f);

    const ColorGrade& g = lighting.grade;
    data.lookSlope = glm::vec4(g.slope, g.saturation);
    data.lookOffset = glm::vec4(g.offset, g.isIdentity() ? 0.0f : 1.0f);
    data.lookPower = glm::vec4(g.power, 0.0f);

    SkySH flat;
    if (!sky) flat = SkySH::constant(lighting.ambientColor);
    const SkySH& sh = sky ? sky->ambient : flat;
    for (int i = 0; i < 9; ++i) data.ambientSH[i] = glm::vec4(sh.c[i], 0.0f);
    if (sky) {
        // Shaders that take one ambient colour (translucent, fluid) get the average.
        data.ambient = glm::vec4(sky->ambientAverage, data.ambient.a);
        data.skyZenith = glm::vec4(sky->zenith, static_cast<float>(sky->kind));
        data.skyHorizon = glm::vec4(sky->horizon, sky->horizonFalloff);
        data.skyGround = glm::vec4(sky->ground, sky->yawRadians);
        data.sunDisc = glm::vec4(sky->sunDisc, sky->sunCosRadius);
        data.sunGlow = glm::vec4(sky->sunGlow, sky->imageScale);
        data.fogColor = glm::vec4(sky->fogColor, 0.0f);
    } else {
        data.fogColor = glm::vec4(lighting.fog.color, 0.0f);
    }
    const Fog& f = lighting.fog;
    if (f.enabled) data.fogColor.a = std::max(f.density, 0.0f);
    data.fogParams = glm::vec4(f.heightFalloff, f.height, std::clamp(f.maxOpacity, 0.0f, 1.0f), f.sunScatter);

    m_buffers[m_slot]->upload(&data, sizeof(data));
}

} // namespace kke
