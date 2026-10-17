#include "kke/ParticleEffects.h"

#include "kke/Application.h"
#include "kke/VulkanCheck.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace kke {

ParticleEffects::ParticleEffects(Application& app, size_t maxParticles) : m_app(app), m_sim(maxParticles) {
    const char* cpu = std::getenv("KKE_PARTICLES_CPU");
    m_sim.setGpuBorn(!cpu || std::strcmp(cpu, "0") == 0);
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
    VkDevice dev = m_app.device().device();
    if (m_gpuPool) vkDestroyDescriptorPool(dev, m_gpuPool, nullptr);
    if (m_gpuSetLayout) vkDestroyDescriptorSetLayout(dev, m_gpuSetLayout, nullptr);
}

void ParticleEffects::clear() {
    m_sim.clear();
    for (GpuKind& g : m_gpu) g.ring.clear(); // every slot dead: drawn as nothing
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
                                       glm::vec4(p.origin, p.vortex) };
        for (std::vector<uint32_t>& pending : g.pending) pending.push_back(slot);
    }
}

Pipeline& ParticleEffects::gpuPipeline(Kind kind) {
    std::unique_ptr<Pipeline>& made = m_gpuPipelines[static_cast<int>(kind)];
    if (made) return *made;
    VkDevice dev = m_app.device().device();
    if (!m_gpuSetLayout) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 1;
        li.pBindings = &b;
        VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_gpuSetLayout));
        VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kKindCount * Renderer::kMaxFramesInFlight };
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

void ParticleEffects::drawGpu(const RenderContext& ctx, Kind kind) {
    GpuKind& g = m_gpu[static_cast<int>(kind)];
    if (g.ring.used() == 0) return;
    Pipeline& pipe = gpuPipeline(kind);
    const uint32_t f = ctx.frameIndex;
    std::vector<uint32_t>& pending = g.pending[f];
    if (g.bufferSlots[f] < g.ring.capacity()) {
        // A new (bigger) buffer for this frame, with every record in it.
        if (g.buffers[f]) m_app.renderer().retire(std::move(g.buffers[f]));
        g.bufferSlots[f] = g.ring.capacity();
        g.buffers[f] = std::make_unique<Buffer>(m_app.device(), g.bufferSlots[f] * sizeof(GpuParticle), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);
        g.buffers[f]->upload(g.records.data(), g.records.size() * sizeof(GpuParticle));
        pending.clear();
        if (!g.sets[f]) {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = m_gpuPool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &m_gpuSetLayout;
            VK_CHECK(vkAllocateDescriptorSets(m_app.device().device(), &ai, &g.sets[f]));
        }
        // This frame's last use of the set has finished (its fence), so it can point at the new buffer.
        VkDescriptorBufferInfo info{ g.buffers[f]->handle(), 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = g.sets[f];
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo = &info;
        vkUpdateDescriptorSets(m_app.device().device(), 1, &w, 0, nullptr);
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
    pipe.bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, g.sets[f] };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.layout(), 0, 3, sets, 0, nullptr);
    const glm::vec4 push[2] = { glm::vec4(m_sim.time(), m_sim.wind()), glm::vec4(static_cast<float>(kind), 0.0f, 0.0f, 0.0f) };
    vkCmdPushConstants(ctx.cmd, pipe.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), push);
    vkCmdDraw(ctx.cmd, 6, static_cast<uint32_t>(g.ring.used()), 0, 0); // one instance per slot; dead ones draw nothing
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
        slot.buffer = std::make_unique<Buffer>(m_app.device(), slot.capacity * sizeof(GpuInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                               VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    slot.buffer->upload(instances.data(), instances.size() * sizeof(GpuInstance));
    Pipeline& pipe = pipeline(kind);
    pipe.bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.layout(), 0, 2, sets, 0, nullptr);
    VkBuffer vb = slot.buffer->handle();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
    vkCmdDraw(ctx.cmd, 6, static_cast<uint32_t>(instances.size()), 0, 0); // six corners, one instance per particle
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
