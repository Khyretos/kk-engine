#include "DestructionModule.h"
#include "kke/Application.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <cmath>
#include <cstring>

namespace kke_demo {

namespace {

struct CubePushConstants {
    glm::mat4 mvp;
    glm::mat4 model;
};

uint32_t wangHash(uint32_t x) {
    x = (x ^ 61u) ^ (x >> 16u);
    x *= 9u;
    x ^= (x >> 4u);
    x *= 0x27d4eb2du;
    x ^= (x >> 15u);
    return x;
}

float hashFloat(uint32_t x) {
    return static_cast<float>(wangHash(x)) / 4294967295.0f;
}

// The whole point of this function: it takes nothing but (seed, index,
// elapsed time) and returns a transform — no accumulated state, no
// per-substep integration. Call it with the same three inputs on any
// machine, any frame rate, any number of physics substeps, and you get
// bit-for-bit (well, float-for-float) the same answer. That's what lets
// a peer reconstruct the whole effect from just {seed, triggerTick}.
glm::mat4 fragmentTransform(uint64_t seed, uint32_t index, float elapsed) {
    uint32_t base = static_cast<uint32_t>(seed) * 747796405u + index * 2891336453u;

    float angle = hashFloat(base) * 6.2831853f;
    float outSpeed = 1.0f + hashFloat(base + 1u) * 3.0f;
    float upSpeed = 2.0f + hashFloat(base + 2u) * 3.0f;
    float spinSpeed = (hashFloat(base + 3u) - 0.5f) * 10.0f;

    glm::vec3 velocity(std::cos(angle) * outSpeed, upSpeed, std::sin(angle) * outSpeed);

    // Closed-form projectile motion (position = v*t + 0.5*g*t^2), not an
    // integrated simulation — another reason this is exactly reproducible
    // from elapsed time alone.
    glm::vec3 position = velocity * elapsed;
    position.y += -0.5f * 4.0f * elapsed * elapsed;
    position.y = std::max(position.y, 0.08f); // rest on the grid instead of falling through it

    glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    t = glm::rotate(t, spinSpeed * elapsed, glm::vec3(0.3f, 1.0f, 0.2f));
    t = glm::scale(t, glm::vec3(0.15f));
    return t;
}

void appendU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

uint64_t readU64(const std::vector<uint8_t>& in, size_t offset) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(in[offset + i]) << (i * 8);
    return v;
}

} // namespace

DestructionModule::DestructionModule(uint32_t fragmentCount, uint64_t seed)
    : m_fragmentCount(fragmentCount), m_seed(seed) {}

void DestructionModule::init(kke::Application& app) {
    m_fragmentMesh = std::make_unique<kke::Mesh>(kke::Mesh::createCube(app.device()));

    kke::PipelineConfig config;
    config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CubePushConstants) };
    config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout() };

    m_pipeline = std::make_unique<kke::Pipeline>(
        app.device(), app.renderer().renderPass(),
        "shaders/cube.vert.spv", "shaders/cube.frag.spv", config);
}

void DestructionModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    m_currentTick = ctx.tickIndex;
    m_fixedDt = ctx.fixedDt;
}

void DestructionModule::trigger(uint64_t atTick) {
    m_triggered = true;
    m_triggerTick = atTick;
}

void DestructionModule::render(const kke::RenderContext& ctx) {
    if (!m_triggered) return;

    float elapsed = static_cast<float>(m_currentTick - m_triggerTick) * m_fixedDt;

    m_pipeline->bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(),
                             0, 2, sets, 0, nullptr);
    m_fragmentMesh->bind(ctx.cmd);

    for (uint32_t i = 0; i < m_fragmentCount; ++i) {
        glm::mat4 model = fragmentTransform(m_seed, i, elapsed);
        CubePushConstants pc{ ctx.proj * ctx.view * model, model };
        vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        m_fragmentMesh->draw(ctx.cmd);
    }
}

void DestructionModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(10, 220), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Destruction");
    ImGui::Text("Seed: %llu", static_cast<unsigned long long>(m_seed));
    ImGui::Text("Fragments: %u", m_fragmentCount);

    if (!m_triggered) {
        if (ImGui::Button("Trigger")) {
            trigger(m_currentTick);
        }
    } else {
        float elapsed = static_cast<float>(m_currentTick - m_triggerTick) * m_fixedDt;
        ImGui::Text("Triggered at tick %llu (%.2fs ago)",
                    static_cast<unsigned long long>(m_triggerTick), elapsed);
        if (ImGui::Button("Reset")) {
            m_triggered = false;
        }
    }
    ImGui::TextWrapped(
        "Replicated state is just the seed + trigger tick above (17 bytes) "
        "— every fragment's position is re-derived from those on whoever "
        "receives them, never sent itself.");
    ImGui::End();
}

std::vector<uint8_t> DestructionModule::serializeReplicatedState() {
    std::vector<uint8_t> data;
    data.reserve(17);
    appendU64(data, m_seed);
    data.push_back(m_triggered ? 1 : 0);
    appendU64(data, m_triggerTick);
    return data;
}

void DestructionModule::deserializeReplicatedState(const std::vector<uint8_t>& data) {
    if (data.size() < 17) return; // malformed/truncated — ignore rather than read out of bounds
    m_seed = readU64(data, 0);
    m_triggered = data[8] != 0;
    m_triggerTick = readU64(data, 9);
}

} // namespace kke_demo
