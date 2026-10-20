#include "kke/modules/ParticleModule.h"
#include "kke/Application.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"

#include <imgui.h>
#include <array>
#include <vector>
#include "kke/ImGuiPlacement.h"
#include "kke/rhi/VulkanDevice.h"

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
    m_gpu = &app.gpu();

    createBuffer();
    createDescriptors();
    createComputePipeline();

    PipelineConfig renderConfig;
    renderConfig.useVertexInput = false; // vertex shader indexes the SSBO directly via gl_VertexIndex
    renderConfig.topology = rhi::Topology::PointList;
    renderConfig.cullMode = rhi::CullMode::None;
    renderConfig.depthTestEnable = true;
    renderConfig.depthWriteEnable = false; // additive-looking transparent sprites shouldn't occlude each other
    renderConfig.blendEnable = true;
    renderConfig.pushConstantRange = { rhi::kVertex, 0, sizeof(RenderPushConstants) };
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
    // The particles, read by the compute step and the draw; made by the RHI device (R1d).
    const rhi::BindingLayout binding{ 0, rhi::BindingType::StorageBuffer, rhi::kCompute | rhi::kVertex };
    m_groupLayout = m_gpu->createBindGroupLayout({ &binding, 1 });
    m_descriptorSetLayout = rhi::VulkanBindGroupLayout::vk(*m_groupLayout);
    const rhi::BindGroup group = m_gpu->createBindGroup(*m_groupLayout);
    const rhi::BindingWrite write{ .binding = 0, .buffer = rhi::VulkanCommandList::buffer(m_particleBuffer->handle()) };
    m_gpu->writeBindGroup(group, *m_groupLayout, { &write, 1 });
    m_descriptorSet = rhi::VulkanGpuDevice::set(group);
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

    m_renderPipeline->bind(*ctx.list);
    ctx.list->bindGroups(*m_renderPipeline, 0, rhi::VulkanCommandList::groups(&m_descriptorSet, 1));
    ctx.list->pushConstants(*m_renderPipeline, rhi::kVertex, 0, sizeof(pc), &pc);
    ctx.list->draw(m_particleCount, 1, 0, 0);
}

void ParticleModule::renderUi() {
    placeNextDebugWindow(ImVec2(340, 10));
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
    if (m_descriptorSet) m_gpu->releaseBindGroup(rhi::VulkanCommandList::group(m_descriptorSet));
    m_groupLayout.reset();

    m_computePipeline = VK_NULL_HANDLE;
    m_computeLayout = VK_NULL_HANDLE;
    m_descriptorSet = VK_NULL_HANDLE;
    m_descriptorSetLayout = VK_NULL_HANDLE;
}

} // namespace kke
