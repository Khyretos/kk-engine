#include "kke/ParticleEffects.h"

#include "kke/Application.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace kke {

ParticleEffects::ParticleEffects(Application& app, size_t maxParticles) : m_app(app), m_max(std::max<size_t>(16, maxParticles)) {
    m_particles.reserve(m_max);
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
    config.customVertexBindings = { { 0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX } };
    config.customVertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, center)) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, color)) },
        { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, params)) },
        { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, velocity)) },
        { 4, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, corner)) },
        { 5, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(GpuVertex, extra)) },
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
        switch (p.kind) {
        case Kind::Smoke:
        case Kind::Glow:
            // Drifts toward the wind's speed, rises while hot, spreads.
            p.velocity += (wind - p.velocity) * std::min(1.0f, p.drag * dt);
            p.velocity.y += p.rise * dt;
            p.radius += p.growth * dt;
            p.growth *= std::max(0.0f, 1.0f - 0.6f * dt);
            p.angle += p.spin * dt;
            break;
        case Kind::Flake: {
            if (p.position.y <= p.floor + 1e-3f && p.velocity.y <= 0.0f) break; // resting on the ground: stays, fades
            // Falls at its own pace, carried by the wind, swaying.
            const glm::vec3 air = wind + glm::vec3(std::sin(p.age * 2.3f + p.seed * 6.2831853f), 0.0f,
                                                   std::cos(p.age * 1.7f + p.seed * 9.1f)) * p.flutter;
            p.velocity.x += (air.x - p.velocity.x) * std::min(1.0f, p.drag * dt);
            p.velocity.z += (air.z - p.velocity.z) * std::min(1.0f, p.drag * dt);
            p.velocity.y *= std::max(0.0f, 1.0f - p.drag * 0.5f * dt);
            p.velocity.y += p.rise * dt;
            p.angle += p.spin * dt;
            p.flipAngle += p.flip * dt;
            break;
        }
        case Kind::Ring:
            p.radius += p.growth * dt;
            break;
        case Kind::Spark:
            p.velocity *= std::max(0.0f, 1.0f - p.drag * dt);
            p.velocity.y += p.rise * dt;
            break;
        }
        p.position += p.velocity * dt;
        // Sparks bounce off the ground they were born near and flakes
        // settle on it (cheap: no collision queries).
        if (p.kind == Kind::Spark && p.velocity.y < 0.0f && p.age > 0.05f && p.position.y < p.floor + 0.02f) {
            p.position.y = p.floor + 0.02f;
            p.velocity.y *= -0.35f;
            p.velocity.x *= 0.6f;
            p.velocity.z *= 0.6f;
        } else if (p.kind == Kind::Flake && p.velocity.y < 0.0f && p.position.y < p.floor + 1e-3f) {
            p.position.y = p.floor + 1e-3f;
            p.velocity = glm::vec3(0.0f);
            p.flipAngle = 0.0f; // lies flat: drawn face-on
            p.flip = 0.0f;
            p.spin = 0.0f;
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
    if (kind == Kind::Smoke || kind == Kind::Flake) {
        // Back to front: nearer ones over further ones.
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
        const glm::vec4 params(p.radius, p.opacity * fade, p.angle, p.seed);
        const glm::vec3 streak = kind == Kind::Spark ? p.velocity * p.stretch + glm::vec3(0.0f, 1e-3f, 0.0f) : glm::vec3(0.0f);
        for (const glm::vec2& c : corners) out.push_back(GpuVertex{ p.position, color, params, streak, c, extra });
    }
}

void ParticleEffects::drawBatch(const RenderContext& ctx, Kind kind, const std::vector<GpuVertex>& vertices, int slotIndex) {
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
    Pipeline& pipe = pipeline(kind);
    pipe.bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.layout(), 0, 2, sets, 0, nullptr);
    VkBuffer vb = slot.buffer->handle();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
    vkCmdDraw(ctx.cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
}

void ParticleEffects::draw(const RenderContext& ctx) {
    if (m_particles.empty()) return;
    const int view = static_cast<int>(ctx.viewIndex);
    // See-through ones first (each sorted back to front), then light.
    for (Kind kind : { Kind::Flake, Kind::Smoke, Kind::Glow, Kind::Ring, Kind::Spark }) {
        build(m_scratch, kind, ctx.cameraPos);
        drawBatch(ctx, kind, m_scratch, view * kKindCount + static_cast<int>(kind));
    }
}

} // namespace kke
