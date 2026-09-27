#include "kke/SkyRenderer.h"

#include "kke/Application.h"
#include "kke/Pipeline.h"
#include "kke/Texture.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <glm/gtc/packing.hpp>

#include <array>

namespace kke {

namespace {
struct SkyPush { glm::mat4 invViewProj; };
} // namespace

SkyRenderer::SkyRenderer(Application& app) : m_app(app) {
    VkDevice dev = app.device().device();
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_setLayout));

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_pool));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_setLayout;
    VK_CHECK(vkAllocateDescriptorSets(dev, &allocInfo, &m_set));

    const std::array<uint16_t, 4> black{ glm::packHalf1x16(0.0f), glm::packHalf1x16(0.0f), glm::packHalf1x16(0.0f), glm::packHalf1x16(1.0f) };
    m_placeholder = std::make_unique<Texture>(app.device(), black.data(), 1, 1, Texture::HalfFloatSky{});
    bindImage(*m_placeholder);

    PipelineConfig pc;
    pc.useVertexInput = false;
    pc.cullMode = VK_CULL_MODE_NONE;
    pc.depthTestEnable = true;  // at the far plane (LESS_OR_EQUAL): only where nothing was drawn
    pc.depthWriteEnable = false;
    pc.pushConstantRange = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyPush) };
    pc.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), m_setLayout };
    m_pipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/sky.vert.spv", "shaders/sky.frag.spv", pc);
}

SkyRenderer::~SkyRenderer() {
    VkDevice dev = m_app.device().device();
    m_pipeline.reset();
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(dev, m_setLayout, nullptr);
}

void SkyRenderer::bindImage(const Texture& texture) {
    VkDescriptorImageInfo info{};
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.imageView = texture.imageView();
    info.sampler = texture.sampler();
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(m_app.device().device(), 1, &write, 0, nullptr);
}

const SkyEnvironment& SkyRenderer::prepare(const Sky& sky, const Fog& fog, const glm::vec3& flatAmbient) {
    const SkyEnvironment& env = m_resolver.resolve(sky, fog, flatAmbient);
    m_env = &env;
    const std::shared_ptr<const SkyImage> wanted = env.kind == Sky::Kind::Image ? env.image : nullptr;
    if (wanted != m_imageSource) {
        // The old image (or the descriptor pointing at it) may still be in
        // use by a frame in flight.
        vkDeviceWaitIdle(m_app.device().device());
        if (wanted) {
            // Full sphere at the image's own width; the GPU does the
            // normalising (SkyEnvironment::imageScale), so this upload is
            // shared by every intensity.
            const std::vector<uint16_t> half = wanted->toHalfRgba(1.0f);
            auto tex = std::make_unique<Texture>(m_app.device(), half.data(), wanted->width(), wanted->fullHeight(), Texture::HalfFloatSky{});
            bindImage(*tex);
            m_image = std::move(tex);
        } else {
            bindImage(*m_placeholder);
            m_image.reset();
        }
        m_imageSource = wanted;
    }
    return env;
}

void SkyRenderer::draw(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj, VkDescriptorSet lighting) {
    if (!m_env || m_env->kind == Sky::Kind::None) return;
    m_pipeline->bind(cmd);
    const std::array<VkDescriptorSet, 2> sets{ lighting, m_set };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(), 0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
    SkyPush pc{ glm::inverse(proj * view) };
    vkCmdPushConstants(cmd, m_pipeline->layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace kke
