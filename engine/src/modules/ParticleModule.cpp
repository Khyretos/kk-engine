#include "kke/modules/ParticleModule.h"
#include "kke/Application.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"

#include <imgui.h>
#include <array>
#include <vector>

namespace kke {

namespace {
struct ParticleGpu {
    glm::vec4 position; // xyz + life
    glm::vec4 velocity; // xyz + maxLife
};

struct ComputePushConstants {
    float dt;
    float time;
    uint32_t particleCount;
    float padding = 0.0f;
};

struct RenderPushConstants {
    glm::mat4 viewProj;
};
} // namespace

ParticleModule::ParticleModule(uint32_t particleCount) : m_particleCount(particleCount) {}

ParticleModule::~ParticleModule() = default;

void ParticleModule::init(Application& app) {
    m_device = &app.device();

    createBuffer();
    createDescriptors();
    createComputePipeline();

    PipelineConfig renderConfig;
    renderConfig.useVertexInput = false; // vertex shader indexes the SSBO directly via gl_VertexIndex
    renderConfig.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    renderConfig.cullMode = VK_CULL_MODE_NONE;
    renderConfig.depthTestEnable = true;
    renderConfig.depthWriteEnable = false; // additive-looking transparent sprites shouldn't occlude each other
    renderConfig.blendEnable = true;
    renderConfig.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(RenderPushConstants) };
    renderConfig.descriptorSetLayouts = { m_descriptorSetLayout };

    m_renderPipeline = std::make_unique<Pipeline>(
        *m_device, app.renderer().renderPass(),
        "shaders/particle.vert.spv", "shaders/particle.frag.spv", renderConfig);
}

void ParticleModule::createBuffer() {
    // Zero-initialized: every particle starts with life <= 0, so the
    // compute shader's "respawn if dead" branch fires for all of them on
    // frame one instead of needing a separate init pass.
    std::vector<ParticleGpu> initial(m_particleCount, ParticleGpu{});
    VkDeviceSize size = sizeof(ParticleGpu) * m_particleCount;

    m_particleBuffer = std::make_unique<Buffer>(
        Buffer::createDeviceLocal(*m_device, initial.data(), size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
}

void ParticleModule::createDescriptors() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(m_device->device(), &layoutInfo, nullptr, &m_descriptorSetLayout));

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(m_device->device(), &poolInfo, nullptr, &m_descriptorPool));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(m_device->device(), &allocInfo, &m_descriptorSet));

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_particleBuffer->handle();
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_device->device(), 1, &write, 0, nullptr);
}

void ParticleModule::createComputePipeline() {
    VkShaderModule computeModule = loadShaderModule(*m_device, "shaders/particle.comp.spv");

    VkPushConstantRange pushRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants) };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    VK_CHECK(vkCreatePipelineLayout(m_device->device(), &layoutInfo, nullptr, &m_computeLayout));

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = computeModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_computeLayout;

    VK_CHECK(vkCreateComputePipelines(m_device->device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_computePipeline));

    vkDestroyShaderModule(m_device->device(), computeModule, nullptr);
}

void ParticleModule::update(const UpdateContext& ctx) {
    m_lastDt = ctx.dt;
    m_totalTime = ctx.totalTime;
}

void ParticleModule::compute(VkCommandBuffer cmd) {
    if (m_paused) return;

    ComputePushConstants pc{ m_lastDt, m_totalTime, m_particleCount, 0.0f };

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_computeLayout, 0, 1, &m_descriptorSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    uint32_t groupCount = (m_particleCount + 255) / 256;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    // The graphics pipeline's vertex shader is about to read what the
    // compute shader just wrote — without this barrier, Vulkan gives no
    // guarantee the writes are visible before the read.
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.buffer = m_particleBuffer->handle();
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                          0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void ParticleModule::render(const RenderContext& ctx) {
    RenderPushConstants pc{ ctx.proj * ctx.view };

    m_renderPipeline->bind(ctx.cmd);
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_renderPipeline->layout(),
                             0, 1, &m_descriptorSet, 0, nullptr);
    vkCmdPushConstants(ctx.cmd, m_renderPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(ctx.cmd, m_particleCount, 1, 0, 0);
}

void ParticleModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(340, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Particles");
    ImGui::Text("%u particles, simulated entirely on the GPU", m_particleCount);
    ImGui::Checkbox("Paused", &m_paused);
    ImGui::End();
}

void ParticleModule::shutdown() {
    if (!m_device) return;
    VkDevice dev = m_device->device();

    if (m_computePipeline) vkDestroyPipeline(dev, m_computePipeline, nullptr);
    if (m_computeLayout) vkDestroyPipelineLayout(dev, m_computeLayout, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(dev, m_descriptorSetLayout, nullptr);

    m_computePipeline = VK_NULL_HANDLE;
    m_computeLayout = VK_NULL_HANDLE;
    m_descriptorPool = VK_NULL_HANDLE;
    m_descriptorSetLayout = VK_NULL_HANDLE;
}

} // namespace kke
