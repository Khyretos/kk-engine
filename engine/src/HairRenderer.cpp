#include "kke/HairRenderer.h"

#include "kke/Application.h"
#include "kke/ShadowMap.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kke {

namespace {

// shaders/hair_common.glsl's HairGpu (std430).
struct HairGpu {
    uint32_t guide, other, phaseBits, pad;
    glm::vec4 offset; // xyz, w = blend
    glm::vec4 color;  // rgb tint, a = length
};
static_assert(sizeof(HairGpu) == 48);

// shaders/hair_common.glsl's Frame, before the guide points.
struct FrameHeader {
    glm::mat4 head;
    glm::vec4 counts, shape, rootColor, tipColor, look;
};
static_assert(sizeof(FrameHeader) == 144);

struct ShadowPush {
    glm::mat4 lightViewProj;
    glm::vec4 lightDir;
};

} // namespace

HairRenderer::HairRenderer(Application& app) : m_app(app) {
    VkDescriptorSetLayoutBinding b[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        b[i].binding = i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{};
    li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    li.bindingCount = 2;
    li.pBindings = b;
    VkDevice dev = m_app.device().device();
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &li, nullptr, &m_setLayout));
    VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * Renderer::kMaxFramesInFlight };
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = Renderer::kMaxFramesInFlight;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    VK_CHECK(vkCreateDescriptorPool(dev, &pi, nullptr, &m_pool));
    for (FrameData& f : m_frames) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &m_setLayout;
        VK_CHECK(vkAllocateDescriptorSets(dev, &ai, &f.set));
    }
    createPipelines();
}

HairRenderer::~HairRenderer() {
    VkDevice dev = m_app.device().device();
    vkDeviceWaitIdle(dev);
    m_pipeline.reset();
    m_shadowPipeline.reset();
    for (FrameData& f : m_frames) f.buffer.reset();
    m_hairBuffer.reset();
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
    if (m_setLayout) vkDestroyDescriptorSetLayout(dev, m_setLayout, nullptr);
}

void HairRenderer::createPipelines() {
    PipelineConfig c;
    c.useVertexInput = false;       // hair.vert builds every vertex from the buffers
    c.cullMode = VK_CULL_MODE_NONE; // ribbons face the camera, either winding
    c.descriptorSetLayouts = { m_app.lightingBuffer().descriptorSetLayout(), m_app.shadowMapSetLayout(), m_setLayout };
    m_pipeline = std::make_unique<Pipeline>(m_app.device(), m_app.renderer().renderPass(), "shaders/hair.vert.spv", "shaders/hair.frag.spv", c);
    PipelineConfig s = ShadowMap::casterConfig();
    s.useVertexInput = false;
    s.cullMode = VK_CULL_MODE_NONE;
    s.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush) };
    s.descriptorSetLayouts = { m_setLayout };
    m_shadowPipeline = std::make_unique<Pipeline>(m_app.device(), m_app.shadowMap().renderPass(), "shaders/hair_shadow.vert.spv", "shaders/shadow.frag.spv", s);
}

void HairRenderer::build(const HairDesc& desc) {
    m_strands.build(desc);
    std::vector<HairGpu> gpu;
    gpu.reserve(m_strands.hairs());
    for (const HairStrands::Hair& h : m_strands.list()) {
        HairGpu g{};
        g.guide = h.guide;
        g.other = h.other;
        std::memcpy(&g.phaseBits, &h.phase, sizeof(float));
        g.offset = glm::vec4(h.offset, h.blend);
        g.color = glm::vec4(h.color, h.length);
        gpu.push_back(g);
    }
    if (gpu.empty()) gpu.push_back(HairGpu{}); // a buffer can't be empty
    // The old buffer may still be read by a frame in flight: the renderer
    // frees it once those frames are done.
    if (m_hairBuffer) m_app.renderer().retire(std::move(m_hairBuffer));
    m_hairBuffer = std::make_unique<Buffer>(Buffer::createDeviceLocal(m_app.device(), gpu.data(), gpu.size() * sizeof(HairGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    for (FrameData& f : m_frames) f.bound = false;
    ++m_version;
}

void HairRenderer::update(const std::vector<glm::vec3>& guides, const glm::mat4& head) {
    const HairStyle& st = m_strands.style();
    const size_t headerFloats = sizeof(FrameHeader) / sizeof(float);
    m_frameBytes.resize(headerFloats + guides.size() * 4);
    FrameHeader h;
    h.head = glm::mat4(glm::mat3(head));
    h.counts = glm::vec4(float(m_strands.pointsPerHair()), float(m_strands.strandVertices()), st.hairWidth, 0.0f);
    h.shape = glm::vec4(st.clump, st.frizz, std::max(st.hairWidth * 3.0f, 0.004f), 0.0f);
    h.rootColor = glm::vec4(st.rootColor, 1.0f);
    h.tipColor = glm::vec4(st.tipColor, st.shine);
    h.look = glm::vec4(st.shift, 0.35f, 0.0f, 0.0f);
    std::memcpy(m_frameBytes.data(), &h, sizeof(h));
    float* p = m_frameBytes.data() + headerFloats;
    for (const glm::vec3& g : guides) {
        *p++ = g.x;
        *p++ = g.y;
        *p++ = g.z;
        *p++ = 1.0f;
    }
    ++m_version;
}

void HairRenderer::prepare(uint32_t frame) {
    FrameData& f = m_frames[frame];
    const size_t bytes = m_frameBytes.size() * sizeof(float);
    if (f.capacity < bytes) {
        f.capacity = bytes * 3 / 2 + 1024;
        f.buffer = std::make_unique<Buffer>(m_app.device(), f.capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        f.bound = false;
        f.version = 0;
    }
    if (f.version != m_version) {
        f.buffer->upload(m_frameBytes.data(), bytes);
        f.version = m_version;
    }
    if (!f.bound) {
        VkDescriptorBufferInfo infos[2] = { { m_hairBuffer->handle(), 0, VK_WHOLE_SIZE }, { f.buffer->handle(), 0, VK_WHOLE_SIZE } };
        VkWriteDescriptorSet w[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = f.set;
            w[i].dstBinding = i;
            w[i].descriptorCount = 1;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(m_app.device().device(), 2, w, 0, nullptr);
        f.bound = true;
    }
}

uint32_t HairRenderer::vertexCount() const {
    const int pts = m_strands.pointsPerHair();
    return pts < 2 ? 0u : uint32_t(m_strands.hairs()) * uint32_t(pts - 1) * 6u;
}

void HairRenderer::draw(const RenderContext& ctx) {
    if (!m_hairBuffer || m_frameBytes.empty() || vertexCount() == 0) return;
    // A pixel's size one metre from the camera, for the shader's minimum width.
    const VkExtent2D e = m_app.renderer().renderExtent();
    const float pixel = 2.0f / (std::max(std::fabs(ctx.proj[1][1]), 1e-4f) * float(std::max(e.height, 1u)));
    if (reinterpret_cast<const FrameHeader*>(m_frameBytes.data())->counts.w != pixel) {
        reinterpret_cast<FrameHeader*>(m_frameBytes.data())->counts.w = pixel;
        ++m_version;
    }
    prepare(ctx.frameIndex);
    m_pipeline->bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, m_frames[ctx.frameIndex].set };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(), 0, 3, sets, 0, nullptr);
    vkCmdDraw(ctx.cmd, vertexCount(), 1, 0, 0);
}

void HairRenderer::drawShadow(const ShadowRenderContext& ctx, const glm::vec3& towardsLight) {
    if (!m_hairBuffer || m_frameBytes.empty() || vertexCount() == 0) return;
    prepare(ctx.frameIndex);
    m_shadowPipeline->bind(ctx.cmd);
    VkDescriptorSet set = m_frames[ctx.frameIndex].set;
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline->layout(), 0, 1, &set, 0, nullptr);
    ShadowPush pc{ ctx.lightViewProj, glm::vec4(glm::normalize(towardsLight), 0.0f) };
    vkCmdPushConstants(ctx.cmd, m_shadowPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(ctx.cmd, vertexCount(), 1, 0, 0);
}

} // namespace kke
