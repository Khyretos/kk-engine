#include "CubeModule.h"
#include "kke/Application.h"
#include "kke/VulkanCheck.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace kke_demo {

namespace {
// Shrunk from { mat4 mvp; mat4 model; } -- see cube.vert's own comment
// for the full account of why (128-byte push constant limit, mvp
// removed as redundant with the now-shared LightingUBO.viewProj, real
// room made for real PBR material properties).
struct CubePushConstants {
    glm::mat4 model;
    float metallic;
    float roughness;
};
struct ShadowPushConstants {
    glm::mat4 lightViewProj;
    glm::mat4 model;
};
} // namespace

void CubeModule::init(kke::Application& app) {
    m_device = app.device().device();
    m_mesh = std::make_unique<kke::Mesh>(kke::Mesh::createCube(app.device()));

    // A real, generated procedural checkerboard, not a placeholder --
    // the first real material texture in this engine's 3D pipeline,
    // deliberately generated rather than loaded from a file: a
    // checkerboard immediately, visibly proves UV mapping is correct
    // (a wrong/flipped/degenerate UV shows up instantly as a distorted
    // or missing checker pattern, unlike a photo texture where subtle
    // UV bugs can hide), and needs no external asset file at all,
    // keeping this self-contained the same way the RmlUi image-loading
    // work generated its own test PNG rather than depending on a
    // pre-supplied asset.
    constexpr uint32_t kCheckerSize = 64;
    constexpr uint32_t kCheckerSquares = 8;
    std::vector<uint8_t> checkerPixels(kCheckerSize * kCheckerSize * 4);
    for (uint32_t y = 0; y < kCheckerSize; ++y) {
        for (uint32_t x = 0; x < kCheckerSize; ++x) {
            uint32_t squareX = x * kCheckerSquares / kCheckerSize;
            uint32_t squareY = y * kCheckerSquares / kCheckerSize;
            bool light = ((squareX + squareY) % 2) == 0;
            uint8_t value = light ? 235 : 60;
            size_t idx = (static_cast<size_t>(y) * kCheckerSize + x) * 4;
            checkerPixels[idx + 0] = value;
            checkerPixels[idx + 1] = value;
            checkerPixels[idx + 2] = value;
            checkerPixels[idx + 3] = 255;
        }
    }
    m_texture = std::make_unique<kke::Texture>(app.device(), checkerPixels.data(), kCheckerSize, kCheckerSize);

    // This texture's own descriptor set -- a small, dedicated pool for
    // exactly one set, the same established pattern already used
    // throughout this codebase for other per-module small pools (not
    // Application's own pool, which is sized only for its own default
    // texture plus this one real consumer -- see Application.cpp's own
    // comment on that pool's maxSets).
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(app.device().device(), &poolInfo, nullptr, &m_texturePool));

    VkDescriptorSetLayout textureLayout = app.materialTextureSetLayout();
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_texturePool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureLayout;
    VK_CHECK(vkAllocateDescriptorSets(app.device().device(), &allocInfo, &m_textureDescriptorSet));

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = m_texture->imageView();
    imageInfo.sampler = m_texture->sampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_textureDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(app.device().device(), 1, &write, 0, nullptr);

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
    config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), app.materialTextureSetLayout() };

    m_pipeline = std::make_unique<kke::Pipeline>(
        app.device(), app.renderer().renderPass(),
        "shaders/cube.vert.spv", "shaders/cube.frag.spv", config);

    // The shadow pass's own pipeline — deliberately minimal (see
    // shadow.vert/frag and this class's own header comment): same
    // cullMode as the main pipeline (matching it for consistency, not
    // because the winding bug above is expected to matter here too —
    // untested either way, kept simple), no descriptor sets at all, a
    // different, smaller push constant layout, and critically a
    // different render pass (the shadow map's own depth-only one, not
    // the swapchain's).
    kke::PipelineConfig shadowConfig;
    shadowConfig.cullMode = VK_CULL_MODE_NONE;
    shadowConfig.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants) };
    m_shadowPipeline = std::make_unique<kke::Pipeline>(
        app.device(), app.shadowMap().renderPass(),
        "shaders/shadow.vert.spv", "shaders/shadow.frag.spv", shadowConfig);
}

void CubeModule::update(const kke::UpdateContext& ctx) {
    if (m_spinning) {
        m_accumulatedAngle += ctx.dt * m_spinSpeedDegPerSec;
    }
}

void CubeModule::render(const kke::RenderContext& ctx) {
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(m_accumulatedAngle), m_spinAxis);
    CubePushConstants pc{ model, m_metallic, m_roughness };

    m_pipeline->bind(ctx.cmd);
    // The cube's own real texture (m_textureDescriptorSet), not
    // ctx.defaultMaterialTextureDescriptorSet -- this is the one real
    // consumer in this engine with a genuine, non-placeholder material
    // texture of its own.
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, m_textureDescriptorSet };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(),
                             0, 3, sets, 0, nullptr);
    vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    m_mesh->bind(ctx.cmd);
    m_mesh->draw(ctx.cmd);
}

void CubeModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(m_accumulatedAngle), m_spinAxis);
    ShadowPushConstants pc{ ctx.lightViewProj, model };

    m_shadowPipeline->bind(ctx.cmd);
    vkCmdPushConstants(ctx.cmd, m_shadowPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    m_mesh->bind(ctx.cmd);
    m_mesh->draw(ctx.cmd);
}

void CubeModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_FirstUseEver);
    ImGui::Begin("Cube");
    ImGui::Checkbox("Spinning", &m_spinning);
    ImGui::SliderFloat("Spin speed", &m_spinSpeedDegPerSec, -180.0f, 180.0f);
    // Real, live PBR controls -- not fixed constants. Sliding
    // "Metallic" from 0 to 1 while watching the cube is the actual,
    // direct way to verify Cook-Torrance is doing something real:
    // near 1.0, the cube's own albedo color should visibly take over
    // the specular highlight color and the diffuse term should nearly
    // vanish; near 0.0, highlights stay a neutral white/light-source
    // color and the surface reads as a normal, colored dielectric.
    ImGui::SliderFloat("Metallic", &m_metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &m_roughness, 0.0f, 1.0f);
    ImGui::End();
}

void CubeModule::shutdown() {
    // m_texture (a real GPU image/view/sampler) cleans itself up via
    // its own RAII destructor once this unique_ptr is reset/destroyed
    // -- nothing to do for it explicitly here. This pool is different:
    // a bare VkDescriptorPool handle with no RAII wrapper of its own,
    // so it needs a real, explicit destroy call, same as every other
    // manually-managed Vulkan handle in this codebase.
    if (m_texturePool) {
        vkDestroyDescriptorPool(m_device, m_texturePool, nullptr);
        m_texturePool = VK_NULL_HANDLE;
    }
}

} // namespace kke_demo
