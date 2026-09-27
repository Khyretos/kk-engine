#include "kke/HairRenderer.h"

#include "kke/Application.h"
#include "kke/ShadowMap.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <tuple>

namespace kke {

namespace {

// shaders/hair_common.glsl's HairGpu (std430).
struct HairGpu {
    uint32_t guide, other, phaseBits, pad;
    glm::vec4 offset; // xyz, w = blend
    glm::vec4 color;  // rgb tint, a = length
    glm::vec4 coil;   // x = turns, y = phase, z = radius scale
};
static_assert(sizeof(HairGpu) == 64);

// shaders/hair_common.glsl's Frame, before the guide points.
struct FrameHeader {
    glm::mat4 head;
    glm::vec4 counts, shape, rootColor, tipColor, look, coil, headSphere;
};
static_assert(sizeof(FrameHeader) == 176);

struct ShadowPush {
    glm::mat4 lightViewProj;
    glm::vec4 lightDir;
};

} // namespace

struct HairRenderer::Shared {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    std::unique_ptr<Pipeline> pipeline, shadowPipeline;
    Shared(Application& app) : device(app.device().device()) {
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
        VK_CHECK(vkCreateDescriptorSetLayout(device, &li, nullptr, &setLayout));
        PipelineConfig c;
        c.useVertexInput = false;       // hair.vert builds every vertex from the buffers
        c.cullMode = VK_CULL_MODE_NONE; // ribbons face the camera, either winding
        c.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), setLayout };
        pipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/hair.vert.spv", "shaders/hair.frag.spv", c);
        PipelineConfig s = ShadowMap::casterConfig();
        s.useVertexInput = false;
        s.cullMode = VK_CULL_MODE_NONE;
        s.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPush) };
        s.descriptorSetLayouts = { setLayout };
        shadowPipeline = std::make_unique<Pipeline>(app.device(), app.shadowMap().renderPass(), "shaders/hair_shadow.vert.spv", "shaders/shadow.frag.spv", s);
    }
    ~Shared() {
        pipeline.reset();
        shadowPipeline.reset();
        if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    }
    Shared(const Shared&) = delete;
    Shared& operator=(const Shared&) = delete;
};

HairRenderer::HairRenderer(Application& app) : m_app(app) {
    {
        static std::mutex lock;
        static std::map<std::tuple<Application*, VkRenderPass, VkRenderPass>, std::weak_ptr<Shared>> cache;
        const std::lock_guard<std::mutex> hold(lock);
        std::weak_ptr<Shared>& slot = cache[{ &app, app.renderer().renderPass(), app.shadowMap().renderPass() }];
        m_shared = slot.lock();
        if (!m_shared) {
            m_shared = std::make_shared<Shared>(app);
            slot = m_shared;
        }
    }
    VkDevice dev = m_app.device().device();
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
        ai.pSetLayouts = &m_shared->setLayout;
        VK_CHECK(vkAllocateDescriptorSets(dev, &ai, &f.set));
    }
}

HairRenderer::~HairRenderer() {
    VkDevice dev = m_app.device().device();
    for (FrameData& f : m_frames) f.buffer.reset();
    m_hairBuffer.reset();
    if (m_pool) vkDestroyDescriptorPool(dev, m_pool, nullptr);
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
        g.coil = glm::vec4(h.coilTurns, h.coilPhase, h.coilScale, 0.0f);
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
    m_frameBytes.resize(headerFloats + guides.size() * 8); // the points, then their frames
    FrameHeader h;
    h.head = glm::mat4(glm::mat3(head));
    h.counts = glm::vec4(float(m_strands.pointsPerHair()), float(m_strands.strandVertices()), st.hairWidth, 0.0f);
    h.shape = glm::vec4(st.clump, st.frizz, std::max(st.hairWidth * 3.0f, 0.004f), 0.0f);
    h.rootColor = glm::vec4(st.rootColor, 1.0f);
    h.tipColor = glm::vec4(st.tipColor, st.shine);
    h.look = glm::vec4(st.shift, 0.35f, float(m_stride), std::sqrt(float(m_stride)));
    h.headSphere = m_strands.headSphere(head);
    h.coil = glm::vec4(st.coil > 0.0f ? st.coilRadius : 0.0f, st.zigzag, 1.0f / (1.0f - std::clamp(st.shrinkage, 0.0f, 0.9f)), float(guides.size()));
    std::memcpy(m_frameBytes.data(), &h, sizeof(h));
    // What draw() needs to judge how big the hair is on screen.
    glm::vec3 lo(1e30f), hi(-1e30f);
    float length = 0.0f;
    const size_t strand = size_t(std::max(m_strands.strandVertices(), 1));
    for (size_t i = 0; i < guides.size(); ++i) {
        lo = glm::min(lo, guides[i]);
        hi = glm::max(hi, guides[i]);
        if (i % strand != 0) length += glm::distance(guides[i], guides[i - 1]);
    }
    m_centre = guides.empty() ? glm::vec3(0.0f) : 0.5f * (lo + hi);
    m_radius = guides.empty() ? 0.0f : 0.5f * glm::distance(lo, hi);
    m_strandLength = guides.size() < strand ? 0.0f : length / float(guides.size() / strand);
    float* p = m_frameBytes.data() + headerFloats;
    for (const glm::vec3& g : guides) {
        *p++ = g.x;
        *p++ = g.y;
        *p++ = g.z;
        *p++ = 1.0f;
    }
    // Which way the coils turn and how far they are pulled out.
    if (st.coil > 0.0f) m_strands.frames(guides, head, m_guideFrames);
    else m_guideFrames.assign(guides.size(), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    std::memcpy(p, m_guideFrames.data(), m_guideFrames.size() * sizeof(glm::vec4));
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
    return pts < 2 ? 0u : uint32_t(drawnHairs()) * uint32_t(pts - 1) * 6u;
}

size_t HairRenderer::drawnHairs() const {
    return (m_strands.hairs() + m_stride - 1) / m_stride;
}

// Level of detail: far away, a head's hairs are ribbons a pixel wide
// piled dozens deep, and drawing them all costs vertex work nobody sees.
// Draw every stride-th hair instead (hairs are stored guide by guide, so
// every guide keeps its share), each wider by the stride's square root so
// the hair stays as full. Close up the stride is 1.
void HairRenderer::chooseDetail(float pixel, const glm::vec3& camera) {
    constexpr float kLayers = 24.0f; // how deep hairs may pile up before some are left out
    const float perPixel = pixel * std::max(glm::distance(camera, m_centre) - m_radius, 0.05f); // metres a pixel spans at the hair
    const float wide = std::max(m_strands.style().hairWidth, 0.75f * perPixel);
    const float area = 3.14159265f * std::max(m_radius * m_radius, 1e-6f);
    const float layers = float(m_strands.hairs()) * wide * m_strandLength / area;
    const uint32_t most = uint32_t(std::max(m_strands.style().hairsPerGuide, 1));
    const uint32_t stride = std::clamp(uint32_t(layers / kLayers), 1u, most);
    if (stride == m_stride) return;
    m_stride = stride;
    FrameHeader* h = reinterpret_cast<FrameHeader*>(m_frameBytes.data());
    h->look.z = float(m_stride);
    h->look.w = std::sqrt(float(m_stride));
    ++m_version;
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
    chooseDetail(pixel, ctx.cameraPos);
    prepare(ctx.frameIndex);
    m_shared->pipeline->bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, m_frames[ctx.frameIndex].set };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shared->pipeline->layout(), 0, 3, sets, 0, nullptr);
    vkCmdDraw(ctx.cmd, vertexCount(), 1, 0, 0);
}

void HairRenderer::drawShadow(const ShadowRenderContext& ctx, const glm::vec3& towardsLight) {
    if (!m_hairBuffer || m_frameBytes.empty() || vertexCount() == 0) return;
    prepare(ctx.frameIndex);
    m_shared->shadowPipeline->bind(ctx.cmd);
    VkDescriptorSet set = m_frames[ctx.frameIndex].set;
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shared->shadowPipeline->layout(), 0, 1, &set, 0, nullptr);
    ShadowPush pc{ ctx.lightViewProj, glm::vec4(glm::normalize(towardsLight), 0.0f) };
    vkCmdPushConstants(ctx.cmd, m_shared->shadowPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    vkCmdDraw(ctx.cmd, vertexCount(), 1, 0, 0);
}

} // namespace kke
