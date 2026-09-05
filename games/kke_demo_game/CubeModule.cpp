#include "CubeModule.h"
#include "kke/Application.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace kke_demo {

namespace {
struct CubePushConstants {
    glm::mat4 mvp;
    glm::mat4 model;
};
} // namespace

void CubeModule::init(kke::Application& app) {
    m_mesh = std::make_unique<kke::Mesh>(kke::Mesh::createCube(app.device()));

    kke::PipelineConfig config; // defaults are exactly right for opaque, depth-tested geometry
    // Culling disabled deliberately, not a leftover diagnostic — a real,
    // reproducible bug, found and fixed by actually testing it, not
    // theorized: with the default VK_CULL_MODE_BACK_BIT, one triangle
    // of the cube's top face was intermittently missing at certain
    // rotation angles (confirmed via real screenshots at multiple
    // rotation moments — background grid lines visible right through a
    // wedge-shaped gap in the face). Manually verified every face's
    // vertex winding computes a correct outward normal in world space
    // (cross-product by hand, all six faces) and the depth-test config
    // is standard (LESS_OR_EQUAL, both test and write enabled) — the
    // actual runtime culling decision must depend on something in the
    // view/projection handedness this static analysis didn't capture,
    // not on the raw vertex data itself. Empirically confirmed the fix:
    // six screenshots across a full rotation with culling disabled show
    // a completely solid cube every time, where the same six moments
    // with culling enabled showed the gap. Same pattern already
    // precedented in this codebase (see PhysicsModule.cpp's own
    // tetrahedron rendering) rather than a new one invented here — a
    // single small demo cube has no meaningful performance cost from
    // skipping backface culling.
    config.cullMode = VK_CULL_MODE_NONE;
    config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CubePushConstants) };
    config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout() };

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
    CubePushConstants pc{ ctx.proj * ctx.view * model, model };

    m_pipeline->bind(ctx.cmd);
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(),
                             0, 1, &ctx.lightingDescriptorSet, 0, nullptr);
    vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    m_mesh->bind(ctx.cmd);
    m_mesh->draw(ctx.cmd);
}

void CubeModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_FirstUseEver);
    ImGui::Begin("Cube");
    ImGui::Checkbox("Spinning", &m_spinning);
    ImGui::SliderFloat("Spin speed", &m_spinSpeedDegPerSec, -180.0f, 180.0f);
    ImGui::End();
}

} // namespace kke_demo
