#include "BackdropModule.h"

#include "kke/Application.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace kke_demo {

namespace {
struct PushConstants { glm::mat4 model; float metallic; float roughness; };
struct ShadowPushConstants { glm::mat4 lightViewProj; glm::mat4 model; };
}

// A unit cube (-0.5..0.5) with one flat color and per-face normals/UVs.
std::unique_ptr<kke::Mesh> BackdropModule::makeBox(kke::Application& app, glm::vec3 color) {
    const glm::vec3 n[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    std::vector<kke::Vertex> verts;
    std::vector<uint32_t> indices;
    for (const glm::vec3& normal : n) {
        glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 v = glm::cross(normal, u);
        uint32_t base = static_cast<uint32_t>(verts.size());
        const glm::vec2 corners[4] = { {-1,-1}, {1,-1}, {1,1}, {-1,1} };
        for (const glm::vec2& c : corners) {
            glm::vec3 p = normal * 0.5f + u * (c.x * 0.5f) + v * (c.y * 0.5f);
            verts.push_back({ p, color, normal, { c.x * 0.5f + 0.5f, c.y * 0.5f + 0.5f } });
        }
        indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
    return std::make_unique<kke::Mesh>(app.device(), verts, indices);
}

void BackdropModule::init(kke::Application& app) {
    kke::PipelineConfig config;
    config.cullMode = VK_CULL_MODE_NONE;
    config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants) };
    config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), app.materialTextureSetLayout() };
    m_pipeline = std::make_unique<kke::Pipeline>(app.device(), app.renderer().renderPass(),
                                                 "shaders/cube.vert.spv", "shaders/cube.frag.spv", config);
    kke::PipelineConfig shadowConfig;
    shadowConfig.cullMode = VK_CULL_MODE_NONE;
    shadowConfig.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants) };
    m_shadowPipeline = std::make_unique<kke::Pipeline>(app.device(), app.shadowMap().renderPass(),
                                                       "shaders/shadow.vert.spv", "shaders/shadow.frag.spv", shadowConfig);

    m_meshes.push_back(makeBox(app, glm::vec3(0.23f, 0.25f, 0.30f)));
    const glm::vec3 colors[] = { {0.85f,0.30f,0.25f}, {0.95f,0.75f,0.30f}, {0.35f,0.75f,0.45f},
                                 {0.30f,0.55f,0.90f}, {0.65f,0.40f,0.85f}, {0.85f,0.85f,0.88f}, {0.90f,0.55f,0.25f} };
    const int count = 7;
    for (int i = 0; i < count; ++i) {
        float a = static_cast<float>(i) / count * 6.2831853f;
        float h = 0.6f + 0.35f * static_cast<float>(i % 3);
        m_blocks.push_back({ glm::vec3(std::cos(a) * 3.2f, h * 0.5f, std::sin(a) * 3.2f), glm::vec3(0.8f, h, 0.8f),
                             static_cast<float>(i) / (count - 1), 0.25f + 0.1f * static_cast<float>(i % 4), 10.0f + 6.0f * i });
        m_meshes.push_back(makeBox(app, colors[i]));
    }
    app.camera().target = glm::vec3(0.0f, 0.6f, 0.0f);
}

void BackdropModule::update(const kke::UpdateContext& ctx) { m_time += ctx.dt; }

glm::mat4 BackdropModule::blockMatrix(const Block& b) const {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), b.position);
    m = glm::rotate(m, glm::radians(b.spin * m_time), glm::vec3(0, 1, 0));
    return glm::scale(m, b.scale);
}

void BackdropModule::render(const kke::RenderContext& ctx) {
    m_pipeline->bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, ctx.defaultMaterialTextureDescriptorSet };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(), 0, 3, sets, 0, nullptr);
    PushConstants floor{ glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.05f, 0)), glm::vec3(14.0f, 0.1f, 14.0f)), 0.0f, 0.85f };
    vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(floor), &floor);
    m_meshes[0]->bind(ctx.cmd);
    m_meshes[0]->draw(ctx.cmd);
    for (size_t i = 0; i < m_blocks.size(); ++i) {
        PushConstants pc{ blockMatrix(m_blocks[i]), m_blocks[i].metallic, m_blocks[i].roughness };
        vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        m_meshes[i + 1]->bind(ctx.cmd);
        m_meshes[i + 1]->draw(ctx.cmd);
    }
}

void BackdropModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_shadowPipeline->bind(ctx.cmd);
    for (size_t i = 0; i < m_blocks.size(); ++i) {
        ShadowPushConstants pc{ ctx.lightViewProj, blockMatrix(m_blocks[i]) };
        vkCmdPushConstants(ctx.cmd, m_shadowPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        m_meshes[i + 1]->bind(ctx.cmd);
        m_meshes[i + 1]->draw(ctx.cmd);
    }
}

} // namespace kke_demo
