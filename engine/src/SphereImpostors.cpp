#include "kke/SphereImpostors.h"

#include "kke/Application.h"

namespace kke {

SphereImpostorRenderer::SphereImpostorRenderer(Application& app) : m_app(app) {
    PipelineConfig config;
    config.cullMode = VK_CULL_MODE_NONE;
    config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout() };
    m_pipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/impostor.vert.spv", "shaders/impostor.frag.spv", config);
}

void SphereImpostorRenderer::draw(const RenderContext& ctx, const std::vector<Sphere>& spheres) {
    if (spheres.empty()) return;
    m_scratch.resize(spheres.size() * 6);
    static const glm::vec2 corners[6] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
    for (size_t i = 0; i < spheres.size(); ++i) {
        const Sphere& s = spheres[i];
        for (int k = 0; k < 6; ++k) m_scratch[i * 6 + k] = Vertex{ s.center, s.color, glm::vec3(s.radius, s.glow, s.roughness), corners[k] };
    }
    uint32_t f = ctx.frameIndex;
    if (m_capacity[f] < m_scratch.size()) {
        m_capacity[f] = std::max<size_t>(m_scratch.size() * 3 / 2, 6 * 256);
        m_buffers[f] = std::make_unique<Buffer>(m_app.device(), m_capacity[f] * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    m_buffers[f]->upload(m_scratch.data(), m_scratch.size() * sizeof(Vertex));
    m_pipeline->bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(), 0, 2, sets, 0, nullptr);
    VkBuffer vb = m_buffers[f]->handle();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
    vkCmdDraw(ctx.cmd, static_cast<uint32_t>(m_scratch.size()), 1, 0, 0);
}

namespace {
struct MeshPush { glm::mat4 model; float metallic; float roughness; };
struct ShadowPush { glm::mat4 lightViewProj; glm::mat4 model; };
} // namespace

DynamicMeshRenderer::DynamicMeshRenderer(Application& app) : m_app(app) {
    PipelineConfig config;
    config.cullMode = VK_CULL_MODE_NONE; // generated surfaces: winding not guaranteed consistent
    config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPush) };
    config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), app.materialTextureSetLayout() };
    m_pipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/cube.vert.spv", "shaders/glow.frag.spv", config);
    PipelineConfig shadow;
    shadow.cullMode = VK_CULL_MODE_NONE;
    shadow.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush) };
    m_shadowPipeline = std::make_unique<Pipeline>(app.device(), app.shadowMap().renderPass(), "shaders/shadow.vert.spv", "shaders/shadow.frag.spv", shadow);
}

void DynamicMeshRenderer::upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    m_vertices = vertices;
    m_indices = indices;
    ++m_version;
}

void DynamicMeshRenderer::ensureUploaded(uint32_t f) {
    FrameBuffers& fb = m_frames[f];
    if (fb.version == m_version || m_indices.empty()) return;
    if (fb.vCap < m_vertices.size()) {
        fb.vCap = m_vertices.size() * 3 / 2 + 64;
        fb.vertices = std::make_unique<Buffer>(m_app.device(), fb.vCap * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    if (fb.iCap < m_indices.size()) {
        fb.iCap = m_indices.size() * 3 / 2 + 192;
        fb.indices = std::make_unique<Buffer>(m_app.device(), fb.iCap * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    fb.vertices->upload(m_vertices.data(), m_vertices.size() * sizeof(Vertex));
    fb.indices->upload(m_indices.data(), m_indices.size() * sizeof(uint32_t));
    fb.version = m_version;
}

void DynamicMeshRenderer::draw(const RenderContext& ctx, const glm::mat4& model, float metallic, float roughness) {
    if (m_indices.empty()) return;
    bindAndDraw(ctx, *m_pipeline, model, metallic, roughness);
}

void DynamicMeshRenderer::drawTranslucent(const RenderContext& ctx, const glm::mat4& model, float roughness) {
    if (m_indices.empty()) return;
    if (!m_absorbPipeline) {
        // Front faces only (CCW seen from outside, like every closed mesh
        // here), depth tested against the opaque scene but not written,
        // so it never hides what's behind it from itself.
        PipelineConfig c;
        c.cullMode = VK_CULL_MODE_BACK_BIT;
        c.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        c.depthWriteEnable = false;
        c.blendEnable = true;
        c.customColorBlend = true;
        c.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPush) };
        c.descriptorSetLayouts = { m_app.lightingBuffer().descriptorSetLayout(), m_app.shadowMapSetLayout(), m_app.materialTextureSetLayout() };
        c.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO; // behind *= transmittance
        c.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
        m_absorbPipeline = std::make_unique<Pipeline>(m_app.device(), m_app.renderer().renderPass(), "shaders/cube.vert.spv",
                                                      "shaders/translucent_absorb.frag.spv", c);
        c.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // behind += light from the surface
        c.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        m_lightPipeline = std::make_unique<Pipeline>(m_app.device(), m_app.renderer().renderPass(), "shaders/cube.vert.spv",
                                                     "shaders/translucent_light.frag.spv", c);
    }
    bindAndDraw(ctx, *m_absorbPipeline, model, 0.0f, roughness);
    bindAndDraw(ctx, *m_lightPipeline, model, 0.0f, roughness);
}

void DynamicMeshRenderer::bindAndDraw(const RenderContext& ctx, Pipeline& pipeline, const glm::mat4& model, float metallic, float roughness) {
    ensureUploaded(ctx.frameIndex);
    FrameBuffers& fb = m_frames[ctx.frameIndex];
    pipeline.bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, ctx.defaultMaterialTextureDescriptorSet };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0, 3, sets, 0, nullptr);
    MeshPush pc{ model, metallic, roughness };
    vkCmdPushConstants(ctx.cmd, pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    VkBuffer vb = fb.vertices->handle();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
    vkCmdBindIndexBuffer(ctx.cmd, fb.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(ctx.cmd, static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);
}

void DynamicMeshRenderer::drawShadow(const ShadowRenderContext& ctx, const glm::mat4& model) {
    if (m_indices.empty()) return;
    ensureUploaded(ctx.frameIndex);
    FrameBuffers& fb = m_frames[ctx.frameIndex];
    m_shadowPipeline->bind(ctx.cmd);
    ShadowPush pc{ ctx.lightViewProj, model };
    vkCmdPushConstants(ctx.cmd, m_shadowPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    VkBuffer vb = fb.vertices->handle();
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
    vkCmdBindIndexBuffer(ctx.cmd, fb.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(ctx.cmd, static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);
}

} // namespace kke
