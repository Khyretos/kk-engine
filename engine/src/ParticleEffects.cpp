#include "kke/ParticleEffects.h"

#include "kke/Application.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"
#include "kke/rhi/VulkanCommandList.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace kke {

ParticleEffects::ParticleEffects(Application& app, size_t maxParticles) : m_app(app), m_sim(maxParticles) {
    const char* cpu = std::getenv("KKE_PARTICLES_CPU");
    m_sim.setGpuBorn(!cpu || std::strcmp(cpu, "0") == 0);
    m_app.addFrameCompute(this, [this](VkCommandBuffer cmd) { step(cmd); });
}

// One pipeline per kind, made the first time that kind is drawn (a game
// with only smoke and sparks never builds the others, nor needs their
// shaders). Smoke and flakes are see-through (premultiplied alpha);
// sparks, glows and rings are light added onto the image.
Pipeline& ParticleEffects::pipeline(Kind kind) {
    std::unique_ptr<Pipeline>& made = m_pipelines[static_cast<int>(kind)];
    if (made) return *made;
    PipelineConfig config;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthWriteEnable = false; // see-through: tested against the scene, never hides what's behind
    config.blendEnable = true;
    config.customVertexBindings = { { 0, sizeof(GpuInstance), VK_VERTEX_INPUT_RATE_INSTANCE } };
    config.customVertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GpuInstance, center)) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GpuInstance, color)) },
        { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(GpuInstance, params)) },
        { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GpuInstance, velocity)) },
        { 5, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(GpuInstance, extra)) },
    };
    config.descriptorSetLayouts = { m_app.lightingBuffer().descriptorSetLayout(), m_app.shadowMapSetLayout() };
    const bool light = kind == Kind::Spark || kind == Kind::Glow || kind == Kind::Ring;
    if (light) {
        config.customColorBlend = true; // light added onto the image
        config.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        config.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    } else {
        config.premultipliedAlpha = true;
    }
    static const char* const kFragments[kKindCount] = { "shaders/effect_smoke.frag.spv", "shaders/effect_spark.frag.spv",
                                                         "shaders/effect_glow.frag.spv", "shaders/effect_flake.frag.spv",
                                                         "shaders/effect_ring.frag.spv" };
    made = std::make_unique<Pipeline>(m_app.device(), m_app.renderer().renderPass(), "shaders/effect.vert.spv", kFragments[static_cast<int>(kind)],
                                      config);
    return *made;
}

// Like DynamicMeshRenderer: an owner that drops one mid-game hands it to
// renderer().retire(); at shutdown the device is idle first.
ParticleEffects::~ParticleEffects() {
    m_app.removeFrameCompute(this);
    if (m_wantDepth) m_app.wantSceneDepth(this, false);
    VkDevice dev = m_app.device().device();
    if (m_stepLayout) vkDeviceWaitIdle(dev); // the step's image and pipelines may still be in use
    if (m_gpuPool) vkDestroyDescriptorPool(dev, m_gpuPool, nullptr);
    if (m_gpuSetLayout) vkDestroyDescriptorSetLayout(dev, m_gpuSetLayout, nullptr);
    for (VkPipeline p : m_stepPipelines)
        if (p) vkDestroyPipeline(dev, p, nullptr);
    if (m_stepPool) vkDestroyDescriptorPool(dev, m_stepPool, nullptr);
    if (m_stepLayout) vkDestroyPipelineLayout(dev, m_stepLayout, nullptr);
    if (m_stepSetLayout) vkDestroyDescriptorSetLayout(dev, m_stepSetLayout, nullptr);
    if (m_noDepthView) vkDestroyImageView(dev, m_noDepthView, nullptr);
    if (m_noDepth) vmaDestroyImage(m_app.device().allocator(), m_noDepth, m_noDepthAlloc);
    if (m_nearest) vkDestroySampler(dev, m_nearest, nullptr);
}

void ParticleEffects::clear() {
    m_sim.clear();
    for (GpuKind& g : m_gpu) {
        g.ring.clear(); // every slot dead: drawn as nothing
        g.stepped = false;
    }
}

size_t ParticleEffects::count() const {
    size_t n = m_sim.count();
    for (const GpuKind& g : m_gpu) n += g.ring.alive(m_sim.time());
    return n;
}

// The GPU particles born since the last frame, each into its kind's ring:
// recorded once here, then written into each frame's buffer once.
void ParticleEffects::takeBorn() {
    m_sim.takeBorn(m_born);
    for (const ParticleSim::Born& b : m_born) {
        const Particle& p = b.particle;
        GpuKind& g = m_gpu[static_cast<int>(p.kind)];
        const uint32_t slot = g.ring.allocate(m_sim.time(), b.time + p.life);
        if (g.records.size() < g.ring.capacity()) g.records.resize(g.ring.capacity());
        const float kindExtra = p.kind == Kind::Flake ? static_cast<float>(p.shape) : (p.flat ? 1.0f : 0.0f);
        g.records[slot] = GpuParticle{ glm::vec4(p.position, b.time),
                                       glm::vec4(p.velocity, p.life),
                                       glm::vec4(p.color, p.opacity),
                                       glm::vec4(p.colorEnd, kindExtra),
                                       glm::vec4(p.drag, p.rise, p.growth, p.spin),
                                       glm::vec4(p.radius, p.kind == Kind::Ring ? p.thickness : p.stretch, p.flutter, p.floor),
                                       glm::vec4(p.angle, p.flipAngle, p.flip, p.seed),
                                       glm::vec4(p.origin, p.vortex),
                                       glm::vec4(p.attract, p.turbulence, p.turbulenceSize, 0.0f) };
        // ENG-F11: stepped each frame when its motion has no closed form; colliding with the last frame's depth.
        const bool stepped = p.attract != 0.0f || p.turbulence != 0.0f || p.collide;
        g.records[slot].step.w = (stepped ? 1.0f : 0.0f) + (p.collide ? 2.0f : 0.0f);
        g.stepped = g.stepped || stepped;
        if (p.collide && !m_wantDepth) {
            m_wantDepth = true;
            m_app.wantSceneDepth(this, true);
        }
        for (std::vector<uint32_t>& pending : g.pending) pending.push_back(slot);
    }
}

Pipeline& ParticleEffects::gpuPipeline(Kind kind) {
    std::unique_ptr<Pipeline>& made = m_gpuPipelines[static_cast<int>(kind)];
    if (made) return *made;
    VkDevice dev = m_app.device().device();
    if (!m_gpuSetLayout) {
        // The records, and the stepped particles' state (ENG-F11).
        std::array<VkDescriptorSetLayoutBinding, 2> b{};
        for (uint32_t i = 0; i < 2; ++i) b[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = static_cast<uint32_t>(b.size());
        li.pBindings = b.data();
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_gpuSetLayout));
        VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * kKindCount * Renderer::kMaxFramesInFlight };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = kKindCount * Renderer::kMaxFramesInFlight;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &size;
        VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_gpuPool));
    }
    // The CPU pipeline's state and fragment shader, with the GPU's vertex stage.
    PipelineConfig config;
    config.useVertexInput = false;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthWriteEnable = false;
    config.blendEnable = true;
    config.descriptorSetLayouts = { m_app.lightingBuffer().descriptorSetLayout(), m_app.shadowMapSetLayout(), m_gpuSetLayout };
    config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, 2 * sizeof(glm::vec4) };
    if (kind == Kind::Spark || kind == Kind::Glow || kind == Kind::Ring) {
        config.customColorBlend = true;
        config.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        config.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    } else {
        config.premultipliedAlpha = true;
    }
    static const char* const kFragments[kKindCount] = { "shaders/effect_smoke.frag.spv", "shaders/effect_spark.frag.spv",
                                                         "shaders/effect_glow.frag.spv", "shaders/effect_flake.frag.spv",
                                                         "shaders/effect_ring.frag.spv" };
    made = std::make_unique<Pipeline>(m_app.device(), m_app.renderer().renderPass(), "shaders/effect_gpu.vert.spv",
                                      kFragments[static_cast<int>(kind)], config);
    return *made;
}

bool ParticleEffects::syncGpu(Kind kind, uint32_t f) {
    GpuKind& g = m_gpu[static_cast<int>(kind)];
    if (g.ring.used() == 0) return false;
    std::vector<uint32_t>& pending = g.pending[f];
    if (g.bufferSlots[f] < g.ring.capacity()) {
        // A new (bigger) buffer for this frame, with every record in it.
        if (g.buffers[f]) m_app.renderer().retire(std::move(g.buffers[f]));
        g.bufferSlots[f] = g.ring.capacity();
        g.buffers[f] = std::make_unique<Buffer>(m_app.device(), g.bufferSlots[f] * sizeof(GpuParticle), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);
        g.buffers[f]->upload(g.records.data(), g.records.size() * sizeof(GpuParticle));
        pending.clear();
    } else if (!pending.empty()) {
        // Only what was born since this frame's buffer was last written, in runs of neighbouring slots.
        std::sort(pending.begin(), pending.end());
        pending.erase(std::unique(pending.begin(), pending.end()), pending.end());
        for (size_t i = 0; i < pending.size();) {
            size_t j = i + 1;
            while (j < pending.size() && pending[j] == pending[j - 1] + 1) ++j;
            g.buffers[f]->uploadAt(pending[i] * sizeof(GpuParticle), &g.records[pending[i]], (j - i) * sizeof(GpuParticle));
            i = j;
        }
        pending.clear();
    }
    return true;
}

void ParticleEffects::drawGpu(const RenderContext& ctx, Kind kind) {
    const uint32_t f = ctx.frameIndex;
    if (!syncGpu(kind, f)) return;
    GpuKind& g = m_gpu[static_cast<int>(kind)];
    Pipeline& pipe = gpuPipeline(kind);
    if (!m_noState) m_noState = std::make_unique<Buffer>(m_app.device(), sizeof(GpuStepped), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    if (!g.sets[f]) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_gpuPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &m_gpuSetLayout;
        VK_CHECK(vkAllocateDescriptorSets(m_app.device().device(), &ai, &g.sets[f]));
    }
    // This frame's records and the state. Rewritten only when one of them is a new buffer: this frame's last
    // use of the set has finished (its fence), and no view of this frame has bound it yet (a new buffer is
    // made before the first view draws).
    const VkBuffer records = g.buffers[f]->handle(), state = (g.state ? g.state : m_noState)->handle();
    if (g.setBuffers[f][0] != records || g.setBuffers[f][1] != state) {
        const std::array<VkDescriptorBufferInfo, 2> info{ { { records, 0, VK_WHOLE_SIZE }, { state, 0, VK_WHOLE_SIZE } } };
        std::array<VkWriteDescriptorSet, 2> w{};
        for (uint32_t i = 0; i < 2; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = g.sets[f];
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[i].pBufferInfo = &info[i];
        }
        vkUpdateDescriptorSets(m_app.device().device(), static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
        g.setBuffers[f][0] = records;
        g.setBuffers[f][1] = state;
    }
    pipe.bind(*ctx.list);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, g.sets[f] };
    ctx.list->bindGroups(pipe, 0, rhi::VulkanCommandList::groups(sets, 3));
    const glm::vec4 push[2] = { glm::vec4(m_sim.time(), m_sim.wind()), glm::vec4(static_cast<float>(kind), 0.0f, 0.0f, 0.0f) };
    ctx.list->pushConstants(pipe, rhi::kVertex, 0, sizeof(push), push);
    ctx.list->draw(6, static_cast<uint32_t>(g.ring.used()), 0, 0); // one instance per slot; dead ones draw nothing
}

namespace {

// The step's per-frame uniforms (shaders/effect_step.glsl's Frame).
struct StepFrame {
    glm::mat4 viewProj, invViewProj;
    glm::vec4 timeStep; // time, seconds since the last step, 1 when the depth is there
    glm::vec4 wind;
    glm::vec4 screen;   // the depth's size
};

} // namespace

VkPipeline ParticleEffects::stepPipeline(bool multisampled) {
    VkPipeline& p = m_stepPipelines[multisampled ? 1 : 0];
    if (p) return p;
    VkShaderModule comp = loadShaderModule(m_app.device(), multisampled ? "shaders/effect_step_ms.comp.spv" : "shaders/effect_step.comp.spv");
    VkComputePipelineCreateInfo cp{};
    cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cp.stage.module = comp;
    cp.stage.pName = "main";
    cp.layout = m_stepLayout;
    VK_CHECK(vkCreateComputePipelines(m_app.device().device(), VK_NULL_HANDLE, 1, &cp, nullptr, &p));
    vkDestroyShaderModule(m_app.device().device(), comp, nullptr);
    return p;
}

// ENG-F11: before the 3D pass, every stepped particle moves one step (shaders/effect_step.glsl), bouncing
// off the last frame's depth when it collides. Kinds with none are left alone; a game with none pays nothing.
void ParticleEffects::step(VkCommandBuffer cmd) {
    takeBorn();
    const float now = m_sim.time();
    const float dt = m_steppedAt < 0.0f ? 0.0f : std::max(0.0f, now - m_steppedAt);
    m_steppedAt = now;
    bool any = false;
    for (const GpuKind& g : m_gpu) any = any || (g.stepped && g.ring.used() > 0);
    if (!any) return;
    VulkanDevice& device = m_app.device();
    VkDevice dev = device.device();
    const uint32_t f = m_app.renderer().currentFrameIndex();
    if (!m_stepLayout) {
        std::array<VkDescriptorSetLayoutBinding, 4> b{};
        const VkDescriptorType types[4] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER };
        for (uint32_t i = 0; i < 4; ++i) b[i] = { i, types[i], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = static_cast<uint32_t>(b.size());
        li.pBindings = b.data();
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_stepSetLayout));
        VkPushConstantRange range{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::vec4) };
        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &m_stepSetLayout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &range;
        VK_CHECK(vkCreatePipelineLayout(dev, &pl, nullptr, &m_stepLayout));
        constexpr uint32_t sets = kKindCount * Renderer::kMaxFramesInFlight;
        const std::array<VkDescriptorPoolSize, 3> sizes{ { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * sets },
                                                           { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sets },
                                                           { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sets } } };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = sets;
        pi.poolSizeCount = static_cast<uint32_t>(sizes.size());
        pi.pPoolSizes = sizes.data();
        VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_stepPool));
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter = si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_nearest));
        // A 1x1 stand-in bound while there is no depth (never read then).
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.extent = { 1, 1, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.format = VK_FORMAT_R32_SFLOAT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        VK_CHECK(vmaCreateImage(device.allocator(), &ii, &ai, &m_noDepth, &m_noDepthAlloc, nullptr));
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = m_noDepth;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ii.format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VK_CHECK(vkCreateImageView(dev, &vi, nullptr, &m_noDepthView));
        VkImageMemoryBarrier nb{};
        nb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        nb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        nb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        nb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        nb.srcQueueFamilyIndex = nb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        nb.image = m_noDepth;
        nb.subresourceRange = vi.subresourceRange;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &nb);
        for (auto& frame : m_stepFrames)
            frame = std::make_unique<Buffer>(device, sizeof(StepFrame), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    if (!m_noState) m_noState = std::make_unique<Buffer>(device, sizeof(GpuStepped), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    const Renderer::LastFrameDepth depth = m_wantDepth ? m_app.renderer().lastFrameDepth() : Renderer::LastFrameDepth{};
    const bool haveDepth = depth.view != VK_NULL_HANDLE;
    StepFrame frame{};
    frame.viewProj = depth.viewProj;
    frame.invViewProj = glm::inverse(depth.viewProj);
    frame.timeStep = glm::vec4(now, dt, haveDepth ? 1.0f : 0.0f, 0.0f);
    frame.wind = glm::vec4(m_sim.wind(), 0.0f);
    frame.screen = glm::vec4(static_cast<float>(depth.extent.width), static_cast<float>(depth.extent.height), 0.0f, 0.0f);
    m_stepFrames[f]->upload(&frame, sizeof(frame));

    // Last frame's draws read the state and its step wrote it; a grown state is copied over first.
    VkMemoryBarrier before{};
    before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    before.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
    for (int k = 0; k < kKindCount; ++k) {
        GpuKind& g = m_gpu[k];
        if (!g.stepped || !syncGpu(static_cast<Kind>(k), f)) continue;
        if (g.stateSlots < g.ring.capacity()) {
            // A bigger state: every slot "born never" (-1), then the old one copied over.
            auto grown = std::make_unique<Buffer>(device, g.ring.capacity() * sizeof(GpuStepped),
                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                  VMA_MEMORY_USAGE_GPU_ONLY);
            vkCmdFillBuffer(cmd, grown->handle(), 0, VK_WHOLE_SIZE, 0xbf800000u); // -1.0f
            if (g.state) {
                VkMemoryBarrier filled{};
                filled.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                filled.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                filled.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &filled, 0, nullptr, 0, nullptr);
                const VkBufferCopy copy{ 0, 0, g.stateSlots * sizeof(GpuStepped) };
                vkCmdCopyBuffer(cmd, g.state->handle(), grown->handle(), 1, &copy);
                m_app.renderer().retire(std::move(g.state));
            }
            VkMemoryBarrier copied{};
            copied.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            copied.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            copied.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &copied, 0, nullptr, 0, nullptr);
            g.state = std::move(grown);
            g.stateSlots = g.ring.capacity();
        }
        if (!g.stepSets[f]) {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = m_stepPool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &m_stepSetLayout;
            VK_CHECK(vkAllocateDescriptorSets(dev, &ai, &g.stepSets[f]));
        }
        const std::array<VkDescriptorBufferInfo, 3> buffers{ { { g.buffers[f]->handle(), 0, VK_WHOLE_SIZE },
                                                               { g.state->handle(), 0, VK_WHOLE_SIZE },
                                                               { m_stepFrames[f]->handle(), 0, VK_WHOLE_SIZE } } };
        const VkDescriptorImageInfo image = haveDepth ? VkDescriptorImageInfo{ m_nearest, depth.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }
                                                      : VkDescriptorImageInfo{ m_nearest, m_noDepthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        std::array<VkWriteDescriptorSet, 4> w{};
        for (uint32_t i = 0; i < 4; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = g.stepSets[f];
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
        }
        w[0].descriptorType = w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[0].pBufferInfo = &buffers[0];
        w[1].pBufferInfo = &buffers[1];
        w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[2].pImageInfo = &image;
        w[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[3].pBufferInfo = &buffers[2];
        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(w.size()), w.data(), 0, nullptr);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, stepPipeline(haveDepth && depth.samples > 1));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_stepLayout, 0, 1, &g.stepSets[f], 0, nullptr);
        const glm::vec4 push(static_cast<float>(k), static_cast<float>(g.ring.used()), 0.0f, 0.0f);
        vkCmdPushConstants(cmd, m_stepLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, static_cast<uint32_t>((g.ring.used() + 63) / 64), 1, 1);
    }
    // Stepped before this frame's draws read where each particle is.
    VkMemoryBarrier after{};
    after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &after, 0, nullptr, 0, nullptr);
}

void ParticleEffects::build(std::vector<GpuInstance>& out, Kind kind, const glm::vec3& eye) const {
    out.clear();
    m_order.clear();
    const std::vector<Particle>& particles = m_sim.particles();
    for (uint32_t i = 0; i < particles.size(); ++i)
        if (particles[i].kind == kind) m_order.push_back(i);
    if (kind == Kind::Smoke || kind == Kind::Flake) {
        // Back to front: nearer ones over further ones.
        std::sort(m_order.begin(), m_order.end(), [&](uint32_t a, uint32_t b) {
            const glm::vec3 da = particles[a].position - eye, db = particles[b].position - eye;
            return glm::dot(da, da) > glm::dot(db, db);
        });
    }
    out.reserve(m_order.size());
    for (uint32_t i : m_order) {
        const Particle& p = particles[i];
        const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
        float fade = 1.0f;
        glm::vec3 color = p.color;
        glm::vec2 extra(0.0f);
        switch (kind) {
        case Kind::Smoke: fade = std::min(1.0f, t * 8.0f) * (1.0f - t) * (1.0f - t); break; // in quickly, out slowly
        case Kind::Spark: fade = 1.0f - t * t; color *= 1.0f - 0.7f * t; break;               // cools
        case Kind::Glow: fade = std::min(1.0f, t * 12.0f) * (1.0f - t); break;
        case Kind::Flake:
            fade = 1.0f - std::max(0.0f, t - 0.75f) * 4.0f; // solid, then gone in the last quarter
            // A card turning over: edge-on (thin) to face-on and back.
            extra = glm::vec2(std::max(0.08f, std::fabs(std::cos(p.flipAngle))), static_cast<float>(p.shape));
            break;
        case Kind::Ring:
            fade = (1.0f - t) * (1.0f - t);
            extra = glm::vec2(p.flat ? -1.0f : -2.0f, p.thickness);
            break;
        }
        if (p.colorEnd.x >= 0.0f) color = glm::mix(color, p.colorEnd, t) * (kind == Kind::Spark ? 1.0f - 0.7f * t : 1.0f);
        float radius = p.radius;
        if (const ParticleSim::OverLife* c = m_sim.curve(p.curve)) {
            // The effect's own curves over life (ENG-G18) replace the kind's.
            radius *= ParticleSim::sample(c->size, t, 1.0f);
            color = ParticleSim::sample(c->color, t, color);
            if (!c->opacity.empty()) fade = ParticleSim::sample(c->opacity, t, 1.0f);
        }
        const glm::vec4 params(radius, p.opacity * fade, p.angle, p.seed);
        const glm::vec3 streak = kind == Kind::Spark ? p.velocity * p.stretch + glm::vec3(0.0f, 1e-3f, 0.0f) : glm::vec3(0.0f);
        out.push_back(GpuInstance{ p.position, color, params, streak, extra });
    }
}

void ParticleEffects::drawBatch(const RenderContext& ctx, Kind kind, const std::vector<GpuInstance>& instances, int slotIndex) {
    if (instances.empty()) return;
    std::vector<Slot>& slots = m_slots[ctx.frameIndex];
    if (slots.size() <= static_cast<size_t>(slotIndex)) slots.resize(static_cast<size_t>(slotIndex) + 1);
    Slot& slot = slots[static_cast<size_t>(slotIndex)];
    if (slot.capacity < instances.size()) {
        if (slot.buffer) m_app.renderer().retire(std::move(slot.buffer));
        slot.capacity = std::max<size_t>(instances.size() * 3 / 2, 256);
        slot.buffer = m_app.gpu().createBuffer({ slot.capacity * sizeof(GpuInstance), rhi::kVertexBuffer, rhi::Memory::Upload });
    }
    slot.buffer->upload(instances.data(), instances.size() * sizeof(GpuInstance));
    Pipeline& pipe = pipeline(kind);
    pipe.bind(*ctx.list);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet };
    ctx.list->bindGroups(pipe, 0, rhi::VulkanCommandList::groups(sets, 2));
    ctx.list->bindVertexBuffer(0, slot.buffer->ref(), 0);
    ctx.list->draw(6, static_cast<uint32_t>(instances.size()), 0, 0); // six corners, one instance per particle
}

void ParticleEffects::draw(const RenderContext& ctx) {
    takeBorn(); // the first view of a frame takes them; the others find none
    const int view = static_cast<int>(ctx.viewIndex);
    // See-through ones first (each sorted back to front), then light; the
    // GPU's of each kind after the CPU's (they are never sorted).
    for (Kind kind : { Kind::Flake, Kind::Smoke, Kind::Glow, Kind::Ring, Kind::Spark }) {
        if (m_sim.count() > 0) {
            build(m_scratch, kind, ctx.cameraPos);
            drawBatch(ctx, kind, m_scratch, view * kKindCount + static_cast<int>(kind));
        }
        drawGpu(ctx, kind);
    }
}

} // namespace kke
