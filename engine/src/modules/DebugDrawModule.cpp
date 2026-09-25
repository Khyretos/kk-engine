#include "kke/modules/DebugDrawModule.h"

#include "kke/Application.h"

#include <cmath>

namespace kke {

void DebugDrawModule::init(Application& app) {
    m_app = &app;
    PipelineConfig config;
    config.cullMode = VK_CULL_MODE_NONE;
    config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4) };
    config.depthWriteEnable = false;
    m_pipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/debug.vert.spv", "shaders/debug.frag.spv", config);
    config.depthTestEnable = false;
    m_pipelineOnTop = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/debug.vert.spv", "shaders/debug.frag.spv", config);
}

void DebugDrawModule::update(const UpdateContext&) {
    m_lines[0].clear();
    m_lines[1].clear();
}

void DebugDrawModule::line(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color, float thickness, bool onTop) {
    m_lines[onTop ? 1 : 0].push_back({ a, b, color, thickness });
}

void DebugDrawModule::box(const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color, float thickness, bool onTop) {
    box(glm::mat4(1.0f), mn, mx, color, thickness, onTop);
}

void DebugDrawModule::box(const glm::mat4& t, const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& color, float thickness, bool onTop) {
    glm::vec3 c[8];
    for (int i = 0; i < 8; ++i) {
        glm::vec3 p((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
        c[i] = glm::vec3(t * glm::vec4(p, 1.0f));
    }
    const int e[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    for (auto& edge : e) line(c[edge[0]], c[edge[1]], color, thickness, onTop);
}

void DebugDrawModule::gridXZ(const glm::vec3& center, float halfSize, float step, const glm::vec3& color, float thickness) {
    if (step <= 0.0f) return;
    int n = static_cast<int>(halfSize / step);
    for (int i = -n; i <= n; ++i) {
        float o = i * step;
        line(center + glm::vec3(o, 0, -halfSize), center + glm::vec3(o, 0, halfSize), color, thickness);
        line(center + glm::vec3(-halfSize, 0, o), center + glm::vec3(halfSize, 0, o), color, thickness);
    }
}

void DebugDrawModule::cross(const glm::vec3& p, float s, const glm::vec3& color, bool onTop) {
    line(p - glm::vec3(s, 0, 0), p + glm::vec3(s, 0, 0), color, 0.02f, onTop);
    line(p - glm::vec3(0, s, 0), p + glm::vec3(0, s, 0), color, 0.02f, onTop);
    line(p - glm::vec3(0, 0, s), p + glm::vec3(0, 0, s), color, 0.02f, onTop);
}

void DebugDrawModule::render(const RenderContext& ctx) {
    size_t total = m_lines[0].size() + m_lines[1].size();
    if (total == 0) return;
    // Expand every line into a quad facing the camera.
    m_scratch.clear();
    m_scratch.reserve(total * 6);
    for (int layer = 0; layer < 2; ++layer) {
        for (const Line& l : m_lines[layer]) {
            glm::vec3 d = l.b - l.a;
            glm::vec3 toCam = ctx.cameraPos - (l.a + l.b) * 0.5f;
            glm::vec3 side = glm::cross(d, toCam);
            float len = glm::length(side);
            side = len > 1e-8f ? side / len * (l.thickness * 0.5f) : glm::vec3(l.thickness * 0.5f, 0, 0);
            Vertex v0{ l.a - side, l.color, glm::vec3(0, 1, 0), { 0, 0 } }, v1{ l.a + side, l.color, glm::vec3(0, 1, 0), { 0, 0 } };
            Vertex v2{ l.b + side, l.color, glm::vec3(0, 1, 0), { 0, 0 } }, v3{ l.b - side, l.color, glm::vec3(0, 1, 0), { 0, 0 } };
            m_scratch.insert(m_scratch.end(), { v0, v1, v2, v0, v2, v3 });
        }
    }
    uint32_t f = ctx.frameIndex;
    if (m_capacity[f] < m_scratch.size()) {
        m_capacity[f] = std::max<size_t>(m_scratch.size() * 3 / 2, 1024);
        m_buffers[f] = std::make_unique<Buffer>(m_app->device(), m_capacity[f] * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    m_buffers[f]->upload(m_scratch.data(), m_scratch.size() * sizeof(Vertex));
    glm::mat4 viewProj = ctx.proj * ctx.view;
    VkBuffer vb = m_buffers[f]->handle();
    VkDeviceSize off = 0;
    uint32_t first = 0;
    for (int layer = 0; layer < 2; ++layer) {
        uint32_t count = static_cast<uint32_t>(m_lines[layer].size() * 6);
        if (count) {
            Pipeline& p = layer == 0 ? *m_pipeline : *m_pipelineOnTop;
            p.bind(ctx.cmd);
            vkCmdPushConstants(ctx.cmd, p.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &viewProj);
            vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
            vkCmdDraw(ctx.cmd, count, 1, first, 0);
        }
        first += count;
    }
}

} // namespace kke
