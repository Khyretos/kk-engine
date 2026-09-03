#include "CubeModule.h"
#include "kke/Application.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace kke_demo {

namespace {
struct CubePushConstants {
    glm::mat4 mvp;
};
} // namespace

void CubeModule::init(kke::Application& app) {
    m_mesh = std::make_unique<kke::Mesh>(kke::Mesh::createCube(app.device()));

    kke::PipelineConfig config; // defaults are exactly right for opaque, depth-tested geometry
    config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CubePushConstants) };

    m_pipeline = std::make_unique<kke::Pipeline>(
        app.device(), app.renderer().renderPass(),
        "shaders/cube.vert.spv", "shaders/cube.frag.spv", config);
}

void CubeModule::update(const kke::UpdateContext& ctx) {
    if (m_spinning) {
        m_accumulatedAngle += ctx.dt * m_spinSpeedDegPerSec;
    }
}

void CubeModule::render(const kke::RenderContext& ctx) {
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(m_accumulatedAngle), m_spinAxis);
    CubePushConstants pc{ ctx.proj * ctx.view * model };

    m_pipeline->bind(ctx.cmd);
    vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    m_mesh->bind(ctx.cmd);
    m_mesh->draw(ctx.cmd);
}

void CubeModule::renderUi() {
    ImGui::Begin("Cube");
    ImGui::Checkbox("Spinning", &m_spinning);
    ImGui::SliderFloat("Spin speed", &m_spinSpeedDegPerSec, -180.0f, 180.0f);
    ImGui::End();
}

} // namespace kke_demo
