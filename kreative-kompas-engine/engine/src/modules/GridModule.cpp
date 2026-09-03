#include "kke/modules/GridModule.h"
#include "kke/Application.h"

namespace kke {

struct GridPushConstants {
    glm::mat4 viewProj;
    glm::vec4 cameraPos;
};

void GridModule::init(Application& app) {
    PipelineConfig config;
    config.useVertexInput = false; // shader synthesizes the quad from gl_VertexIndex
    config.cullMode = VK_CULL_MODE_NONE; // visible from above or below
    config.depthTestEnable = true;
    config.depthWriteEnable = false; // transparent — don't occlude things behind it
    config.blendEnable = true;
    config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GridPushConstants) };

    m_pipeline = std::make_unique<Pipeline>(
        app.device(), app.renderer().renderPass(),
        "shaders/grid.vert.spv", "shaders/grid.frag.spv", config);
}

void GridModule::render(const RenderContext& ctx) {
    GridPushConstants pc{ ctx.proj * ctx.view, glm::vec4(ctx.cameraPos, 0.0f) };

    m_pipeline->bind(ctx.cmd);
    vkCmdPushConstants(ctx.cmd, m_pipeline->layout(),
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(pc), &pc);
    vkCmdDraw(ctx.cmd, 6, 1, 0, 0);
}

} // namespace kke
