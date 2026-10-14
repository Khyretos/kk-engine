#include "kke/ParticleEffects.h"

#include "kke/Application.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace kke {

ParticleEffects::ParticleEffects(Application& app, size_t maxParticles) : m_app(app), m_sim(maxParticles) {}

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
ParticleEffects::~ParticleEffects() = default;

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
    if (m_sim.count() == 0) return;
    const int view = static_cast<int>(ctx.viewIndex);
    // See-through ones first (each sorted back to front), then light.
    for (Kind kind : { Kind::Flake, Kind::Smoke, Kind::Glow, Kind::Ring, Kind::Spark }) {
        build(m_scratch, kind, ctx.cameraPos);
        drawBatch(ctx, kind, m_scratch, view * kKindCount + static_cast<int>(kind));
    }
}

} // namespace kke
