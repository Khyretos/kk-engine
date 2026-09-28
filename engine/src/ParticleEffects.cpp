#include "kke/ParticleEffects.h"

#include "kke/Application.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace kke {

ParticleEffects::ParticleEffects(Application& app, size_t maxParticles) : m_app(app), m_max(std::max<size_t>(16, maxParticles)) {
    m_particles.reserve(m_max);
    PipelineConfig config;
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthWriteEnable = false; // see-through: tested against the scene, never hides what's behind
    config.blendEnable = true;
    config.customVertexBindings = { { 0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX } };
    config.customVertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, center)) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, color)) },
        { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, params)) },
        { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, velocity)) },
        { 4, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, corner)) },
    };
    config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout() };
    PipelineConfig smoke = config;
    smoke.premultipliedAlpha = true;
    m_smoke = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/effect.vert.spv", "shaders/effect_smoke.frag.spv", smoke);
    PipelineConfig spark = config;
    spark.customColorBlend = true; // light added onto the image
    spark.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    spark.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    m_spark = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/effect.vert.spv", "shaders/effect_spark.frag.spv", spark);
}

// Like DynamicMeshRenderer: an owner that drops one mid-game hands it to
// renderer().retire(); at shutdown the device is idle first.
ParticleEffects::~ParticleEffects() = default;

float ParticleEffects::random01() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xffffffu) / static_cast<float>(0x1000000);
}

void ParticleEffects::emit(const Particle& p) {
    if (m_particles.size() >= m_max) {
        // Full: the oldest makes room (smoke that's nearly gone anyway).
        auto oldest = std::max_element(m_particles.begin(), m_particles.end(),
                                       [](const Particle& a, const Particle& b) { return a.age / a.life < b.age / b.life; });
        *oldest = p;
        return;
    }
    m_particles.push_back(p);
}

void ParticleEffects::smoke(const glm::vec3& position, const glm::vec3& velocity, const glm::vec3& color, float radius, float life, float opacity) {
    Particle p;
    p.kind = Kind::Smoke;
    p.position = position;
    const glm::vec3 jitter(random01() - 0.5f, random01() * 0.5f, random01() - 0.5f);
    p.velocity = velocity + jitter * 0.8f;
    p.color = color * (0.92f + 0.08f * random01());
    p.radius = radius * (0.8f + 0.4f * random01());
    p.growth = radius * (1.4f + random01());
    p.opacity = opacity;
    p.life = life * (0.8f + 0.4f * random01());
    p.spin = (random01() - 0.5f) * 1.2f;
    p.angle = random01() * 6.2831853f;
    p.seed = random01();
    emit(p);
}

void ParticleEffects::sparks(const glm::vec3& position, const glm::vec3& normal, int count, float speed, const glm::vec3& along) {
    const glm::vec3 n = glm::length(normal) > 1e-4f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.kind = Kind::Spark;
        p.position = position;
        // A cone around the normal, dragged along the scrape.
        glm::vec3 d(random01() * 2.0f - 1.0f, random01() * 2.0f - 1.0f, random01() * 2.0f - 1.0f);
        d = glm::normalize(n * 1.2f + d * 0.9f + glm::vec3(1e-4f));
        p.velocity = d * speed * (0.4f + 0.8f * random01()) + along;
        const float heat = 0.6f + 0.4f * random01();
        p.color = glm::vec3(9.0f, 4.2f, 1.2f) * heat;
        p.radius = 0.012f + 0.01f * random01();
        p.growth = 0.0f;
        p.opacity = 1.0f;
        p.life = 0.25f + 0.45f * random01();
        p.drag = 0.6f;
        p.rise = -9.81f;
        p.stretch = 0.035f;
        emit(p);
    }
}

void ParticleEffects::update(float dt, const glm::vec3& wind) {
    if (dt <= 0.0f) return;
    for (Particle& p : m_particles) {
        p.age += dt;
        if (p.kind == Kind::Smoke) {
            // Drifts toward the wind's speed, rises while hot, spreads.
            p.velocity += (wind - p.velocity) * std::min(1.0f, p.drag * dt);
            p.velocity.y += p.rise * dt;
            p.radius += p.growth * dt;
            p.growth *= std::max(0.0f, 1.0f - 0.6f * dt);
            p.angle += p.spin * dt;
        } else {
            p.velocity *= std::max(0.0f, 1.0f - p.drag * dt);
            p.velocity.y += p.rise * dt;
        }
        p.position += p.velocity * dt;
        // Sparks bounce off the ground plane they were born near (cheap:
        // no collision queries; they only live half a second).
        if (p.kind == Kind::Spark && p.velocity.y < 0.0f && p.age > 0.05f && p.position.y < 0.02f) {
            p.position.y = 0.02f;
            p.velocity.y *= -0.35f;
            p.velocity.x *= 0.6f;
            p.velocity.z *= 0.6f;
        }
    }
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(), [](const Particle& p) { return p.age >= p.life; }),
                      m_particles.end());
}

void ParticleEffects::clear() { m_particles.clear(); }

void ParticleEffects::build(std::vector<GpuVertex>& out, Kind kind, const glm::vec3& eye) const {
    out.clear();
    m_order.clear();
    for (uint32_t i = 0; i < m_particles.size(); ++i)
        if (m_particles[i].kind == kind) m_order.push_back(i);
    if (kind == Kind::Smoke) {
        // Back to front: nearer puffs over further ones.
        std::sort(m_order.begin(), m_order.end(), [&](uint32_t a, uint32_t b) {
            const glm::vec3 da = m_particles[a].position - eye, db = m_particles[b].position - eye;
            return glm::dot(da, da) > glm::dot(db, db);
        });
    }
    static const glm::vec2 corners[6] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
    out.reserve(m_order.size() * 6);
    for (uint32_t i : m_order) {
        const Particle& p = m_particles[i];
        const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
        // Smoke fades in quickly and out slowly; sparks cool.
        const float fade = kind == Kind::Smoke ? std::min(1.0f, t * 8.0f) * (1.0f - t) * (1.0f - t) : 1.0f - t * t;
        const glm::vec3 color = kind == Kind::Spark ? p.color * (1.0f - 0.7f * t) : p.color;
        const glm::vec4 params(p.radius, p.opacity * fade, p.angle, p.seed);
        const glm::vec3 streak = kind == Kind::Spark ? p.velocity * p.stretch + glm::vec3(0.0f, 1e-3f, 0.0f) : glm::vec3(0.0f);
        for (const glm::vec2& c : corners) out.push_back(GpuVertex{ p.position, color, params, streak, c });
    }
}

void ParticleEffects::drawBatch(const RenderContext& ctx, Pipeline& pipeline, const std::vector<GpuVertex>& vertices, int slotIndex) {
    if (vertices.empty()) return;
    std::vector<Slot>& slots = m_slots[ctx.frameIndex];
    if (slots.size() <= static_cast<size_t>(slotIndex)) slots.resize(static_cast<size_t>(slotIndex) + 1);
    Slot& slot = slots[static_cast<size_t>(slotIndex)];
    if (slot.capacity < vertices.size()) {
        if (slot.buffer) m_app.renderer().retire(std::move(slot.buffer));
        slot.capacity = std::max<size_t>(vertices.size() * 3 / 2, 6 * 256);
        slot.buffer = std::make_unique<Buffer>(m_app.device(), slot.capacity * sizeof(GpuVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                               VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    slot.buffer->upload(vertices.data(), vertices.size() * sizeof(GpuVertex));
    pipeline.bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0, 2, sets, 0, nullptr);
    VkBuffer vb = slot.buffer->handle();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
    vkCmdDraw(ctx.cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
}

void ParticleEffects::draw(const RenderContext& ctx) {
    if (m_particles.empty()) return;
    const int view = static_cast<int>(ctx.viewIndex);
    build(m_scratch, Kind::Smoke, ctx.cameraPos);
    drawBatch(ctx, *m_smoke, m_scratch, view * 2);
    build(m_scratch, Kind::Spark, ctx.cameraPos);
    drawBatch(ctx, *m_spark, m_scratch, view * 2 + 1);
}

} // namespace kke
