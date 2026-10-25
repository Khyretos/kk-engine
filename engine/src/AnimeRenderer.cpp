#include "kke/AnimeRenderer.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/Mesh.h"
#include "kke/ShadowMap.h"
#include "kke/Texture.h"
#include "kke/rhi/VulkanCommandList.h"
#include "kke/rhi/VulkanDevice.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>

namespace kke {

namespace {

// shaders/anime_common.glsl's AnimePush.
struct AnimePush {
    glm::mat4 model;
    glm::vec4 head, forward, look;
};
static_assert(sizeof(AnimePush) == 112);

// shaders/anime_common.glsl's ToonParams (std140).
struct ToonGpu {
    glm::vec4 color, shade, params, rim, rim2, emission, outline, outline2;
};
static_assert(sizeof(ToonGpu) == 128);

struct ShadowPush {
    glm::mat4 lightViewProj;
    glm::mat4 model;
};

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

struct AnimeRenderer::Shared {
    std::unique_ptr<rhi::BindGroupLayout> group;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    std::unique_ptr<Pipeline> lit, litTwoSided, blend, outline, shadow;
    std::unique_ptr<Texture> flatNormal, black;
    explicit Shared(Application& app) {
        rhi::BindingLayout b[7];
        for (uint32_t i = 0; i < 6; ++i) b[i] = { i, rhi::BindingType::Texture, rhi::kVertex | rhi::kFragment };
        b[6] = { 6, rhi::BindingType::UniformBuffer, rhi::kVertex | rhi::kFragment };
        group = app.gpu().createBindGroupLayout(b);
        setLayout = rhi::VulkanBindGroupLayout::vk(*group);
        PipelineConfig c;
        c.frontFace = rhi::FrontFace::CounterClockwise; // as ModelModule's (BUG-040)
        c.cullMode = rhi::CullMode::Back;
        c.pushConstantRange = { rhi::kVertex | rhi::kFragment, 0, sizeof(AnimePush) };
        c.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), setLayout };
        const VkRenderPass pass = app.renderer().renderPass();
        lit = std::make_unique<Pipeline>(app.device(), pass, "shaders/anime.vert.spv", "shaders/anime.frag.spv", c);
        c.cullMode = rhi::CullMode::None;
        litTwoSided = std::make_unique<Pipeline>(app.device(), pass, "shaders/anime.vert.spv", "shaders/anime.frag.spv", c);
        PipelineConfig bl = c;
        bl.blendEnable = true;
        bl.depthWriteEnable = false;
        blend = std::make_unique<Pipeline>(app.device(), pass, "shaders/anime.vert.spv", "shaders/anime.frag.spv", bl);
        PipelineConfig o = c;
        o.cullMode = rhi::CullMode::Front;
        outline = std::make_unique<Pipeline>(app.device(), pass, "shaders/anime_outline.vert.spv", "shaders/anime_outline.frag.spv", o);
        PipelineConfig s = ShadowMap::casterConfig();
        s.cullMode = rhi::CullMode::None;
        s.pushConstantRange = { rhi::kVertex, 0, sizeof(ShadowPush) };
        shadow = std::make_unique<Pipeline>(app.device(), app.shadowMap().renderPass(), "shaders/shadow.vert.spv", "shaders/shadow.frag.spv", s);
        const uint8_t flat[4] = { 128, 128, 255, 255 };
        flatNormal = std::make_unique<Texture>(app.device(), flat, 1, 1, Texture::LinearData{});
        const uint8_t none[4] = { 0, 0, 0, 255 };
        black = std::make_unique<Texture>(app.device(), none, 1, 1);
    }
};

AnimeRenderer::AnimeRenderer(Application& app) : m_app(app) {
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

AnimeRenderer::~AnimeRenderer() {
    for (Material& m : m_materials)
        if (m.set) m_app.gpu().releaseBindGroup(rhi::VulkanCommandList::group(m.set));
}

void AnimeRenderer::build(const VrmAvatar& avatar) {
    for (Material& m : m_materials) {
        if (m.params) m_app.renderer().retire(std::move(m.params));
        if (m.set) m_app.gpu().releaseBindGroup(rhi::VulkanCommandList::group(m.set));
    }
    for (Part& p : m_parts)
        if (p.indices) m_app.renderer().retire(std::move(p.indices));
    m_materials.clear();
    m_parts.clear();

    m_materials.resize(avatar.toon.size());
    for (size_t i = 0; i < avatar.toon.size(); ++i) {
        Material& m = m_materials[i];
        m.toon = avatar.toon[i];
        // VRoid names its materials by part: "..._Face_00_SKIN", "..._HAIR_01", "..._EyeIris_00_EYE".
        const std::string name = upper(i < avatar.model.materials.size() ? avatar.model.materials[i].name : "");
        const bool face = name.find("FACE") != std::string::npos;
        if (name.find("EYE") != std::string::npos && name.find("EYELINE") == std::string::npos && name.find("EYELASH") == std::string::npos)
            m.kind = Kind::Eye;
        else if (face && name.find("SKIN") != std::string::npos) m.kind = Kind::Face;
        else if (face) m.kind = Kind::FacePart;
        else if (name.find("HAIR") != std::string::npos) m.kind = Kind::Hair;
        m.params = std::make_unique<Buffer>(m_app.device(), sizeof(ToonGpu), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        writeParams(m);
        const Texture& white = m_app.defaultWhiteTexture();
        auto tex = [&](const std::string& path, bool linear, const Texture& fallback) -> const Texture& {
            if (path.empty()) return fallback;
            const Texture* t = m_app.cachedTexture(path, linear);
            return t ? *t : fallback;
        };
        const Texture* maps[6] = { &tex(m.toon.mainTexture, false, white), &tex(m.toon.shadeTexture, false, white),
                                   &tex(m.toon.normalTexture, true, *m_shared->flatNormal), &tex(m.toon.matcapTexture, false, *m_shared->black),
                                   &tex(m.toon.emissionTexture, false, white), &tex(m.toon.outlineWidthTexture, true, white) };
        const rhi::BindGroup group = m_app.gpu().createBindGroup(*m_shared->group);
        if (!group.handle) {
            log::get("AnimeRenderer")->warn("no room for another material set");
            continue;
        }
        rhi::BindingWrite writes[7];
        for (uint32_t b = 0; b < 6; ++b) writes[b] = { .binding = b, .texture = rhi::VulkanGpuDevice::texture(*maps[b]) };
        writes[6] = { .binding = 6, .buffer = rhi::VulkanCommandList::buffer(m.params->handle()), .range = sizeof(ToonGpu) };
        m_app.gpu().writeBindGroup(group, *m_shared->group, writes);
        m.set = rhi::VulkanGpuDevice::set(group);
    }

    uint64_t offset = 0;
    m_weld.assign(avatar.model.meshes.size(), {});
    for (uint32_t i = 0; i < avatar.model.meshes.size(); ++i) {
        const ModelMesh& mesh = avatar.model.meshes[i];
        Part p;
        p.mesh = i;
        p.material = mesh.material;
        p.indexCount = static_cast<uint32_t>(mesh.indices.size());
        p.vertexOffset = offset;
        offset += mesh.vertices.size() * sizeof(Vertex);
        if (!mesh.indices.empty())
            p.indices = std::make_unique<Buffer>(Buffer::createDeviceLocal(m_app.device(), mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t),
                                                                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
        m_parts.push_back(std::move(p));
        // Twins: vertices at the same place (a UV seam, a hard edge) share
        // one outline normal, so the hull doesn't split there.
        std::unordered_map<uint64_t, uint32_t> first;
        std::vector<uint32_t>& weld = m_weld[i];
        weld.resize(mesh.vertices.size());
        for (uint32_t v = 0; v < mesh.vertices.size(); ++v) {
            const glm::ivec3 q = glm::ivec3(glm::round(mesh.vertices[v].position * 20000.0f)); // 0.05 mm
            const uint64_t key = (uint64_t(uint32_t(q.x) & 0x1fffff) << 42) | (uint64_t(uint32_t(q.y) & 0x1fffff) << 21) | uint64_t(uint32_t(q.z) & 0x1fffff);
            weld[v] = first.emplace(key, v).first->second;
        }
    }
    // Blended parts last, in the order the avatar asks (renderQueue).
    std::stable_sort(m_parts.begin(), m_parts.end(), [&](const Part& a, const Part& b) {
        auto rank = [&](const Part& p) {
            const VrmToon& t = m_materials[p.material].toon;
            return t.alpha == VrmToon::Alpha::Blend ? t.renderQueue : 0;
        };
        return rank(a) < rank(b);
    });
    m_bytes.assign(offset, 0);
    for (FrameData& f : m_frames) f.version = 0;
    m_built = true;
    ++m_version;
}

void AnimeRenderer::writeParams(Material& m) {
    const VrmToon& t = m.toon;
    ToonGpu g{};
    g.color = glm::vec4(t.color.r * m.tint.r * m_look.tint.r, t.color.g * m.tint.g * m_look.tint.g, t.color.b * m.tint.b * m_look.tint.b, t.color.a);
    g.shade = glm::vec4(t.shadeColor * m.tint * m_look.tint, t.shadeShift);
    g.params = glm::vec4(t.shadeToony, t.receiveShadow, t.giIntensity, t.alpha == VrmToon::Alpha::Opaque ? -1.0f : (t.alpha == VrmToon::Alpha::Cutout ? t.cutoff : 0.02f));
    g.rim = glm::vec4(t.rimColor, t.rimFresnelPower);
    g.rim2 = glm::vec4(t.rimLift, t.rimLightingMix, t.normalTexture.empty() ? 0.0f : t.normalScale, static_cast<float>(m.kind));
    g.emission = glm::vec4(t.emissionColor, t.lightColorAttenuation);
    // The look's outline where the material has none; never on the eyes or
    // what is painted on the face, nor on see-through parts.
    float width = t.outlineWidth > 0.0f && !t.outlineScreenWidth ? t.outlineWidth : m_look.outline;
    if (m.kind == Kind::Eye || m.kind == Kind::FacePart || t.alpha == VrmToon::Alpha::Blend) width = 0.0f;
    g.outline = glm::vec4(t.outlineColor, width);
    g.outline2 = glm::vec4(t.outlineWidth > 0.0f && t.outlineFixedColor ? 1.0f : 0.0f, t.outlineLightingMix, 0.0f,
                           static_cast<float>(static_cast<int>(t.alpha)));
    m.params->upload(&g, sizeof(g));
}

void AnimeRenderer::tintMaterials(const std::string& name, const glm::vec3& tint) {
    const std::string want = upper(name);
    for (size_t i = 0; i < m_materials.size(); ++i) {
        // The material's name isn't kept; its kind and textures are. Match on the kind words.
        Material& m = m_materials[i];
        const bool hit = (want == "HAIR" && m.kind == Kind::Hair) || (want == "EYE" && m.kind == Kind::Eye) ||
                         (want == "FACE" && m.kind == Kind::Face);
        if (!hit) continue;
        m.tint = tint;
        writeParams(m);
    }
}

void AnimeRenderer::update(const std::vector<std::vector<ModelVertex>>& posed, const glm::mat4& figure, const glm::mat4& head) {
    if (!m_built) return;
    m_figure = figure;
    // The head's sphere: a little behind the bone's head, towards the face's middle.
    const glm::vec3 headPos(head[3]);
    const glm::vec3 up = glm::normalize(glm::vec3(head[1]));
    m_headForward = glm::normalize(glm::vec3(figure * glm::vec4(glm::vec3(0.0f, 0.0f, 1.0f), 0.0f)));
    m_headCentre = glm::vec3(figure * glm::vec4(headPos + up * 0.07f, 1.0f));
    uint8_t* out = m_bytes.data();
    for (size_t i = 0; i < posed.size() && i < m_weld.size(); ++i) {
        const std::vector<ModelVertex>& vs = posed[i];
        const std::vector<uint32_t>& weld = m_weld[i];
        m_smooth.assign(vs.size(), glm::vec3(0.0f));
        for (size_t v = 0; v < vs.size(); ++v) m_smooth[weld[v]] += vs[v].normal;
        for (size_t v = 0; v < vs.size(); ++v) {
            Vertex o;
            o.position = vs[v].position;
            const glm::vec3 s = m_smooth[weld[v]];
            o.color = glm::dot(s, s) > 1e-12f ? glm::normalize(s) : vs[v].normal;
            o.normal = vs[v].normal;
            o.uv = vs[v].uv;
            std::memcpy(out, &o, sizeof(Vertex));
            out += sizeof(Vertex);
        }
    }
    ++m_version;
}

void AnimeRenderer::upload(uint32_t frame) {
    FrameData& f = m_frames[frame];
    if (f.version == m_version) return;
    if (f.capacity < m_bytes.size()) {
        if (f.vertices) m_app.renderer().retire(std::move(f.vertices));
        f.capacity = m_bytes.size();
        f.vertices = std::make_unique<Buffer>(m_app.device(), std::max<size_t>(f.capacity, 16), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    if (!m_bytes.empty()) f.vertices->upload(m_bytes.data(), m_bytes.size());
    f.version = m_version;
}

void AnimeRenderer::draw(const RenderContext& ctx) {
    if (!m_built || !m_visible || m_version == 0 || !ctx.list) return;
    upload(ctx.frameIndex);
    const FrameData& f = m_frames[ctx.frameIndex];
    if (!f.vertices) return;
    AnimePush push{ m_figure, glm::vec4(m_headCentre, m_look.faceSmooth), glm::vec4(m_headForward, 1.0f),
                    glm::vec4(m_look.toony, m_look.ring, m_look.rim, m_look.faceOutline) };
    VkDescriptorSet sets[2] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet };
    const uint32_t stages = rhi::kVertex | rhi::kFragment;
    auto drawWith = [&](Pipeline& pipe, auto&& want) {
        bool bound = false;
        for (const Part& p : m_parts) {
            const Material& m = m_materials[p.material];
            if (!p.indexCount || !m.set || !want(m)) continue;
            if (!bound) {
                pipe.bind(*ctx.list);
                ctx.list->bindGroups(pipe, 0, rhi::VulkanCommandList::groups(sets, 2));
                ctx.list->pushConstants(pipe, stages, 0, sizeof(push), &push);
                bound = true;
            }
            ctx.list->bindGroups(pipe, 2, rhi::VulkanCommandList::groups(&m.set, 1));
            ctx.list->bindVertexBuffer(0, rhi::VulkanCommandList::buffer(f.vertices->handle()), p.vertexOffset);
            ctx.list->bindIndexBuffer(rhi::VulkanCommandList::buffer(p.indices->handle()), 0, rhi::IndexType::U32);
            ctx.list->drawIndexed(p.indexCount, 1, 0, 0, 0);
        }
    };
    using A = VrmToon::Alpha;
    drawWith(*m_shared->lit, [](const Material& m) { return m.toon.alpha != A::Blend && !m.toon.doubleSided; });
    drawWith(*m_shared->litTwoSided, [](const Material& m) { return m.toon.alpha != A::Blend && m.toon.doubleSided; });
    drawWith(*m_shared->outline, [&](const Material& m) {
        if (m.toon.alpha == A::Blend || m.kind == Kind::Eye || m.kind == Kind::FacePart) return false;
        return (m.toon.outlineWidth > 0.0f && !m.toon.outlineScreenWidth) || m_look.outline > 0.0f;
    });
    drawWith(*m_shared->blend, [](const Material& m) { return m.toon.alpha == A::Blend; });
}

void AnimeRenderer::drawShadow(const ShadowRenderContext& ctx) {
    if (!m_built || !m_visible || m_version == 0 || !ctx.list) return;
    upload(ctx.frameIndex);
    const FrameData& f = m_frames[ctx.frameIndex];
    if (!f.vertices) return;
    m_shared->shadow->bind(*ctx.list);
    const ShadowPush push{ ctx.lightViewProj, m_figure };
    ctx.list->pushConstants(*m_shared->shadow, rhi::kVertex, 0, sizeof(push), &push);
    for (const Part& p : m_parts) {
        const Material& m = m_materials[p.material];
        if (!p.indexCount || m.toon.alpha == VrmToon::Alpha::Blend || m.kind == Kind::FacePart || m.kind == Kind::Eye) continue;
        ctx.list->bindVertexBuffer(0, rhi::VulkanCommandList::buffer(f.vertices->handle()), p.vertexOffset);
        ctx.list->bindIndexBuffer(rhi::VulkanCommandList::buffer(p.indices->handle()), 0, rhi::IndexType::U32);
        ctx.list->drawIndexed(p.indexCount, 1, 0, 0, 0);
    }
}

} // namespace kke
