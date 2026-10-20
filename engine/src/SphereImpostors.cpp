#include "kke/SphereImpostors.h"

#include "kke/Application.h"
#include "kke/rhi/VulkanDevice.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <tuple>

namespace kke {

namespace {

// Pipelines every DynamicMeshRenderer of an application shares, per render
// pass: compiling them for each mesh took about 15 ms a mesh, so a scene
// of twenty meshes hitched when it was built.
template <typename Make>
std::shared_ptr<Pipeline> sharedPipeline(Application& app, VkRenderPass pass, const char* what, Make make) {
    static std::mutex lock;
    static std::map<std::tuple<Application*, VkRenderPass, std::string>, std::weak_ptr<Pipeline>> cache;
    const std::lock_guard<std::mutex> hold(lock);
    std::weak_ptr<Pipeline>& slot = cache[{ &app, pass, what }];
    if (std::shared_ptr<Pipeline> p = slot.lock()) return p;
    std::shared_ptr<Pipeline> p = make();
    slot = p;
    return p;
}

} // namespace

SphereImpostorRenderer::SphereImpostorRenderer(Application& app) : m_app(app) {
    PipelineConfig config;
    config.cullMode = rhi::CullMode::None;
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
        m_buffers[f] = m_app.gpu().createBuffer({ m_capacity[f] * sizeof(Vertex), rhi::kVertexBuffer, rhi::Memory::Upload });
    }
    m_buffers[f]->upload(m_scratch.data(), m_scratch.size() * sizeof(Vertex));
    m_pipeline->bind(*ctx.list);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet };
    ctx.list->bindGroups(*m_pipeline, 0, rhi::VulkanCommandList::groups(sets, 2));
    ctx.list->bindVertexBuffer(0, m_buffers[f]->ref(), 0);
    ctx.list->draw(static_cast<uint32_t>(m_scratch.size()), 1, 0, 0);
}

namespace {
struct MeshPush { glm::mat4 model; float metallic; float roughness; };
// cloth.frag's push block: MeshPush, padding, then the fabric (vertex and fragment stages).
struct ClothPush { glm::mat4 model; float metallic, roughness, pad0, pad1; glm::vec4 sheen, weave; };
struct ShadowPush { glm::mat4 lightViewProj; glm::mat4 model; };
} // namespace

DynamicMeshRenderer::DynamicMeshRenderer(Application& app) : m_app(app) {
    m_pipeline = sharedPipeline(app, app.renderer().renderPass(), "mesh", [&app] {
        PipelineConfig config;
        config.cullMode = rhi::CullMode::None; // generated surfaces: winding not guaranteed consistent
        config.pushConstantRange = { rhi::kVertex, 0, sizeof(MeshPush) };
        config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), app.materialTextureSetLayout() };
        return std::make_shared<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/cube.vert.spv", "shaders/glow.frag.spv", config);
    });
    m_shadowPipeline = sharedPipeline(app, app.shadowMap().renderPass(), "mesh shadow", [&app] {
        PipelineConfig shadow = ShadowMap::casterConfig();
        shadow.pushConstantRange = { rhi::kVertex, 0, sizeof(ShadowPush) };
        return std::make_shared<Pipeline>(app.device(), app.shadowMap().renderPass(), "shaders/shadow.vert.spv", "shaders/shadow.frag.spv", shadow);
    });
}

DynamicMeshRenderer::~DynamicMeshRenderer() { m_app.forgetRayDraws(this); }

void DynamicMeshRenderer::upload(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    m_vertices = vertices;
    m_indices = indices;
    ++m_version;
    // Bounds and average colour, for KKE ray tracing's stand-in box.
    m_lo = glm::vec3(1e30f);
    m_hi = glm::vec3(-1e30f);
    glm::vec3 sum(0.0f);
    for (const Vertex& v : vertices) {
        m_lo = glm::min(m_lo, v.position);
        m_hi = glm::max(m_hi, v.position);
        sum += glm::pow(glm::max(v.color, glm::vec3(0.0f)), glm::vec3(2.2f)); // sRGB vertex colours
    }
    m_color = vertices.empty() ? glm::vec3(1.0f) : sum / static_cast<float>(vertices.size());
    if (vertices.empty()) m_lo = m_hi = glm::vec3(0.0f);
}

DynamicMeshRenderer::RayView DynamicMeshRenderer::rayView(uint32_t frame) const {
    RayView v;
    const FrameBuffers& fb = m_frames[frame % Renderer::kMaxFramesInFlight];
    if (fb.vertices && fb.indices && m_app.device().rayQuery()) {
        v.vertices = rhi::VulkanBuffer::deviceAddress(*fb.vertices);
        v.indices = rhi::VulkanBuffer::deviceAddress(*fb.indices);
        v.vertexBuffer = fb.vertices.get();
    }
    v.vertexCount = static_cast<uint32_t>(m_vertices.size());
    v.indexCount = static_cast<uint32_t>(m_indices.size());
    v.version = fb.version;
    v.lo = m_lo;
    v.hi = m_hi;
    v.color = m_color;
    return v;
}

void DynamicMeshRenderer::ensureUploaded(uint32_t f) {
    FrameBuffers& fb = m_frames[f];
    if (fb.version == m_version || m_indices.empty()) return;
    // With ray query the buffers are also what classic ray tracing traces.
    const VkBufferUsageFlags rays = m_app.device().rayQuery()
                                        ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                        : 0;
    if (fb.vCap < m_vertices.size()) {
        fb.vCap = m_vertices.size() * 3 / 2 + 64;
        fb.vertices = m_app.vulkanGpu().createBuffer({ fb.vCap * sizeof(Vertex), rhi::kVertexBuffer, rhi::Memory::Upload }, nullptr, rays);
    }
    if (fb.iCap < m_indices.size()) {
        fb.iCap = m_indices.size() * 3 / 2 + 192;
        fb.indices = m_app.vulkanGpu().createBuffer({ fb.iCap * sizeof(uint32_t), rhi::kIndexBuffer, rhi::Memory::Upload }, nullptr, rays);
    }
    fb.vertices->upload(m_vertices.data(), m_vertices.size() * sizeof(Vertex));
    fb.indices->upload(m_indices.data(), m_indices.size() * sizeof(uint32_t));
    fb.version = m_version;
}

void DynamicMeshRenderer::draw(const RenderContext& ctx, const glm::mat4& model, float metallic, float roughness) {
    if (m_indices.empty()) return;
    bindAndDraw(ctx, *m_pipeline, model, metallic, roughness);
    if (m_app.recordingRays() && ctx.viewIndex == 0) m_app.recordRayDraw(this, model);
}

void DynamicMeshRenderer::drawTranslucent(const RenderContext& ctx, const glm::mat4& model, float roughness, float ior) {
    if (m_indices.empty()) return;
    if (!m_absorbPipeline) {
        // Front faces only (CCW seen from outside, like every closed mesh
        // here), depth tested against the opaque scene but not written,
        // so it never hides what's behind it from itself.
        Application& app = m_app;
        auto config = [&app](rhi::BlendFactor src, rhi::BlendFactor dst) {
            PipelineConfig c;
            c.cullMode = rhi::CullMode::Back;
            c.frontFace = rhi::FrontFace::CounterClockwise;
            c.depthWriteEnable = false;
            c.blendEnable = true;
            c.customColorBlend = true;
            c.pushConstantRange = { rhi::kVertex, 0, sizeof(MeshPush) };
            c.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), app.materialTextureSetLayout() };
            c.srcColorBlendFactor = src;
            c.dstColorBlendFactor = dst;
            return c;
        };
        m_absorbPipeline = sharedPipeline(app, app.renderer().renderPass(), "mesh absorb", [&] {
            // behind *= transmittance
            return std::make_shared<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/cube.vert.spv", "shaders/translucent_absorb.frag.spv",
                                              config(rhi::BlendFactor::Zero, rhi::BlendFactor::SrcColor));
        });
        m_lightPipeline = sharedPipeline(app, app.renderer().renderPass(), "mesh light", [&] {
            // behind += light from the surface
            return std::make_shared<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/cube.vert.spv", "shaders/translucent_light.frag.spv",
                                              config(rhi::BlendFactor::One, rhi::BlendFactor::One));
        });
        m_refractPipeline = sharedPipeline(app, app.renderer().renderPass(), "mesh refract", [&] {
            // behind = the scene where the refracted ray leads x transmittance
            return std::make_shared<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/cube.vert.spv", "shaders/translucent_refract.frag.spv",
                                              config(rhi::BlendFactor::One, rhi::BlendFactor::Zero));
        });
    }
    if (ctx.refractionRays && !m_refractRaysPipeline) {
        Application& app = m_app;
        m_refractRaysPipeline = sharedPipeline(app, app.renderer().renderPass(), "mesh refract rays", [&app] {
            PipelineConfig c;
            c.cullMode = rhi::CullMode::Back;
            c.frontFace = rhi::FrontFace::CounterClockwise;
            c.depthWriteEnable = false;
            c.blendEnable = true;
            c.customColorBlend = true;
            c.pushConstantRange = { rhi::kVertex, 0, sizeof(MeshPush) };
            c.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), app.materialTextureSetLayout(),
                                       app.refractionRayLayout() };
            c.srcColorBlendFactor = rhi::BlendFactor::One;
            c.dstColorBlendFactor = rhi::BlendFactor::Zero;
            return std::make_shared<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/cube.vert.spv",
                                              "shaders/translucent_refract_rays.frag.spv", c);
        });
    }
    // The refracting pass gets the index of refraction where the others get metallic.
    if (ctx.refractionRays) bindAndDraw(ctx, *m_refractRaysPipeline, model, ior, roughness, ctx.refractionRays);
    else if (ctx.refraction) bindAndDraw(ctx, *m_refractPipeline, model, ior, roughness);
    else bindAndDraw(ctx, *m_absorbPipeline, model, 0.0f, roughness);
    bindAndDraw(ctx, *m_lightPipeline, model, 0.0f, roughness);
}

void DynamicMeshRenderer::drawCloth(const RenderContext& ctx, const Fabric& fabric, const glm::mat4& model) {
    if (m_indices.empty()) return;
    if (!m_clothPipeline) {
        Application& app = m_app;
        m_clothPipeline = sharedPipeline(app, app.renderer().renderPass(), "mesh cloth", [&app] {
            PipelineConfig c;
            c.cullMode = rhi::CullMode::None; // both sides of a sheet are seen
            c.pushConstantRange = { rhi::kVertex | rhi::kFragment, 0, sizeof(ClothPush) };
            c.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), app.materialTextureSetLayout() };
            return std::make_shared<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/cube.vert.spv", "shaders/cloth.frag.spv", c);
        });
    }
    ensureUploaded(ctx.frameIndex);
    FrameBuffers& fb = m_frames[ctx.frameIndex];
    m_clothPipeline->bind(*ctx.list);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, ctx.defaultMaterialTextureDescriptorSet };
    ctx.list->bindGroups(*m_clothPipeline, 0, rhi::VulkanCommandList::groups(sets, 3));
    ClothPush pc{ model, 0.0f, fabric.roughness, 0.0f, 0.0f, glm::vec4(fabric.sheenColor, fabric.sheen),
                  glm::vec4(float(fabric.weave), fabric.weaveScale, fabric.fuzz, fabric.specular) };
    ctx.list->pushConstants(*m_clothPipeline, rhi::kVertex | rhi::kFragment, 0, sizeof(pc), &pc);
    ctx.list->bindVertexBuffer(0, fb.vertices->ref(), 0);
    ctx.list->bindIndexBuffer(fb.indices->ref(), 0, rhi::IndexType::U32);
    ctx.list->drawIndexed(static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);
    if (m_app.recordingRays() && ctx.viewIndex == 0) m_app.recordRayDraw(this, model);
}

void DynamicMeshRenderer::bindAndDraw(const RenderContext& ctx, Pipeline& pipeline, const glm::mat4& model, float metallic, float roughness,
                                      VkDescriptorSet raySet) {
    ensureUploaded(ctx.frameIndex);
    FrameBuffers& fb = m_frames[ctx.frameIndex];
    pipeline.bind(*ctx.list);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, ctx.defaultMaterialTextureDescriptorSet };
    ctx.list->bindGroups(pipeline, 0, rhi::VulkanCommandList::groups(sets, 3));
    if (raySet) ctx.list->bindGroups(pipeline, 3, rhi::VulkanCommandList::groups(&raySet, 1));
    MeshPush pc{ model, metallic, roughness };
    ctx.list->pushConstants(pipeline, rhi::kVertex, 0, sizeof(pc), &pc);
    ctx.list->bindVertexBuffer(0, fb.vertices->ref(), 0);
    ctx.list->bindIndexBuffer(fb.indices->ref(), 0, rhi::IndexType::U32);
    ctx.list->drawIndexed(static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);
}

void DynamicMeshRenderer::drawShadow(const ShadowRenderContext& ctx, const glm::mat4& model) {
    if (m_indices.empty()) return;
    ensureUploaded(ctx.frameIndex);
    FrameBuffers& fb = m_frames[ctx.frameIndex];
    m_shadowPipeline->bind(*ctx.list);
    ShadowPush pc{ ctx.lightViewProj, model };
    ctx.list->pushConstants(*m_shadowPipeline, rhi::kVertex, 0, sizeof(pc), &pc);
    ctx.list->bindVertexBuffer(0, fb.vertices->ref(), 0);
    ctx.list->bindIndexBuffer(fb.indices->ref(), 0, rhi::IndexType::U32);
    ctx.list->drawIndexed(static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);
}

} // namespace kke
