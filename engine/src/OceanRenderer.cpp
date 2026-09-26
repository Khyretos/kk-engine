#include "kke/OceanRenderer.h"

#include "kke/Application.h"

#include <cmath>
#include <vector>

namespace kke {

namespace {
struct OceanPush { glm::vec4 data[8]; };
struct SkyPush { glm::mat4 invViewProj; glm::vec4 sunDir; };
} // namespace

OceanRenderer::OceanRenderer(Application& app, int cells, float extent) : m_cellSize(extent / static_cast<float>(cells)) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    verts.reserve(static_cast<size_t>(cells + 1) * (cells + 1));
    const float half = extent * 0.5f;
    for (int z = 0; z <= cells; ++z)
        for (int x = 0; x <= cells; ++x)
            verts.push_back(Vertex{ { x * m_cellSize - half, 0.0f, z * m_cellSize - half }, glm::vec3(1.0f), { 0, 1, 0 }, { 0, 0 } });
    for (int z = 0; z < cells; ++z)
        for (int x = 0; x < cells; ++x) {
            uint32_t a = z * (cells + 1) + x, b = a + 1, c = a + (cells + 1), d = c + 1;
            idx.insert(idx.end(), { a, c, b, b, c, d });
        }
    m_grid = std::make_unique<Mesh>(app.device(), verts, idx);

    PipelineConfig oc;
    oc.cullMode = VK_CULL_MODE_NONE;
    oc.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(OceanPush) };
    oc.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout() };
    m_ocean = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/ocean.vert.spv", "shaders/ocean.frag.spv", oc);

    PipelineConfig sc;
    sc.useVertexInput = false;
    sc.cullMode = VK_CULL_MODE_NONE;
    sc.depthTestEnable = false;
    sc.depthWriteEnable = false;
    sc.pushConstantRange = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyPush) };
    m_sky = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/sky.vert.spv", "shaders/sky.frag.spv", sc);
}

void OceanRenderer::drawSky(const RenderContext& ctx, const glm::vec3& sunDirection) {
    m_sky->bind(ctx.cmd);
    SkyPush pc{ glm::inverse(ctx.proj * ctx.view), glm::vec4(glm::normalize(sunDirection), 0.0f) };
    vkCmdPushConstants(ctx.cmd, m_sky->layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
}

void OceanRenderer::drawOcean(const RenderContext& ctx, const OceanWaves& waves, float time, const glm::vec3& cameraPos) {
    std::vector<glm::vec4> gpu;
    waves.toGpu(gpu);
    OceanPush pc{};
    for (int i = 0; i < 7; ++i) pc.data[i] = gpu[i];
    // Snap the grid origin to whole cells: vertices always sample the same
    // wave positions as the camera moves, so the surface never "swims".
    glm::vec2 origin(std::floor(cameraPos.x / m_cellSize) * m_cellSize, std::floor(cameraPos.z / m_cellSize) * m_cellSize);
    pc.data[7] = glm::vec4(time, origin.x, origin.y, waves.seaLevel);
    m_ocean->bind(ctx.cmd);
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ocean->layout(), 0, 1, &ctx.lightingDescriptorSet, 0, nullptr);
    vkCmdPushConstants(ctx.cmd, m_ocean->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    m_grid->bind(ctx.cmd);
    m_grid->draw(ctx.cmd);
}

} // namespace kke
