#include "kke/modules/ModelModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/Picking.h"
#include "kke/VulkanCheck.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <map>
#include <tuple>

namespace kke {

namespace {
// Matches model.vert/model.frag: material = (metallic, roughness, overlay
// tile size in metres or 0, overlay strength); tint.rgb = the material's
// base colour times the instance tint (sRGB).
struct PushConstants { glm::mat4 model; glm::vec4 material; glm::vec4 tint; };
struct ShadowPushConstants { glm::mat4 lightViewProj; glm::mat4 model; };

// The vertex colour slot carries the *overlay position*: where the vertex
// sits in the object's own rest shape (model space). model.vert projects
// the grid overlay from it, so the grid sticks to the object when it's
// moved, rotated or broken, instead of sliding through it as the old
// world-space projection did. The colour itself is a push constant.
Vertex toVertex(const ModelVertex& v) {
    return Vertex{ v.position, v.position, v.normal, v.uv };
}

// Uniform scale of a transform (Synty FBX imports carry 0.01-style scales).
float instanceScale(const glm::mat4& t) {
    return (glm::length(glm::vec3(t[0])) + glm::length(glm::vec3(t[1])) + glm::length(glm::vec3(t[2]))) / 3.0f;
}
} // namespace

void ModelModule::init(Application& app) {
    m_app = &app;
    m_device = app.device().device();

    PipelineConfig config;
    config.cullMode = VK_CULL_MODE_BACK_BIT;
    // ModelAsset outputs counter-clockwise front faces (glTF/OpenGL
    // convention). The projection flips Y (proj[1][1] *= -1), but Vulkan
    // also measures winding in framebuffer space, whose Y points down —
    // the two flips cancel, so front faces stay counter-clockwise. This
    // said CLOCKWISE until BUG-040: every model was drawn inside-out
    // (outer faces culled, the inside of the far faces visible).
    config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants) };
    // Set 3 = world overlay texture (same one-sampler layout as set 2).
    config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), app.materialTextureSetLayout(),
                                    app.materialTextureSetLayout() };
    m_pipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/model.vert.spv", "shaders/model.frag.spv", config);

    // Skeleton overlay: same shader, drawn on top of everything.
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTestEnable = false;
    config.depthWriteEnable = false;
    m_bonePipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/model.vert.spv", "shaders/model.frag.spv", config);

    PipelineConfig shadowConfig;
    shadowConfig.cullMode = VK_CULL_MODE_NONE;
    shadowConfig.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants) };
    m_shadowPipeline = std::make_unique<Pipeline>(app.device(), app.shadowMap().renderPass(), "shaders/shadow.vert.spv", "shaders/shadow.frag.spv", shadowConfig);

    // Instanced variants (OPTIMIZATION.md #25): kke::Vertex at binding 0,
    // InstanceGpu (model matrix + tint) per instance at binding 1.
    {
        auto binding0 = Vertex::bindingDescription();
        auto attrs0 = Vertex::attributeDescriptions();
        VkVertexInputBindingDescription binding1{ 1, sizeof(InstanceGpu), VK_VERTEX_INPUT_RATE_INSTANCE };
        std::vector<VkVertexInputAttributeDescription> attrs(attrs0.begin(), attrs0.end());
        for (uint32_t c = 0; c < 4; ++c) attrs.push_back({ 8 + c, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(sizeof(glm::vec4) * c) });
        attrs.push_back({ 12, 1, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(InstanceGpu, tint)) });
        PipelineConfig inst;
        inst.cullMode = VK_CULL_MODE_BACK_BIT;
        inst.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        inst.customVertexBindings = { binding0, binding1 };
        inst.customVertexAttributes = attrs;
        inst.pushConstantRange = config.pushConstantRange;
        inst.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), app.materialTextureSetLayout(),
                                      app.materialTextureSetLayout() };
        m_instancedPipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/model_instanced.vert.spv",
                                                         "shaders/model.frag.spv", inst);
        PipelineConfig instShadow = shadowConfig;
        instShadow.customVertexBindings = inst.customVertexBindings;
        instShadow.customVertexAttributes = attrs;
        m_instancedShadowPipeline = std::make_unique<Pipeline>(app.device(), app.shadowMap().renderPass(), "shaders/shadow_instanced.vert.spv",
                                                               "shaders/shadow.frag.spv", instShadow);
    }

    // A bone is drawn as a box from its parent's origin to its own:
    // unit length along +Y, thin in X/Z, scaled/rotated per bone.
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    const glm::vec3 n[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    for (const glm::vec3& normal : n) {
        glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 v = glm::cross(normal, u);
        uint32_t base = static_cast<uint32_t>(verts.size());
        for (glm::vec2 c : { glm::vec2(-1,-1), glm::vec2(1,-1), glm::vec2(1,1), glm::vec2(-1,1) }) {
            glm::vec3 p = normal * 0.5f + u * (c.x * 0.5f) + v * (c.y * 0.5f) + glm::vec3(0, 0.5f, 0);
            verts.push_back({ p, glm::vec3(1.0f, 0.82f, 0.25f), normal, { 0, 0 } });
        }
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
    m_boneMesh = std::make_unique<Mesh>(app.device(), verts, idx);
}

// Shared with every other module through Application's texture cache.
VkDescriptorSet ModelModule::textureSetFor(const std::string& path) { return m_app->textureSet(path); }

ModelModule::ModelId ModelModule::load(const std::string& path, const ModelLoadOptions& options) {
    if (!m_app) {
        log::get(name())->error("load('{}') called before init()", path);
        return 0;
    }
    if (auto it = m_modelByPath.find(path); it != m_modelByPath.end()) return it->second;
    ModelData data;
    try {
        data = loadModel(path, options);
    } catch (const std::exception& e) {
        log::get(name())->error("{}", e.what());
        return 0;
    }
    return add(std::move(data), path);
}

ModelModule::ModelId ModelModule::add(ModelData modelData, const std::string& key) {
    if (!m_app) {
        log::get(name())->error("add('{}') called before init()", key);
        return 0;
    }
    if (auto it = m_modelByPath.find(key); it != m_modelByPath.end()) return it->second;
    const std::string& path = key;
    auto loaded = std::make_unique<LoadedModel>();
    loaded->data = std::move(modelData);
    const ModelData& data = loaded->data;
    for (const ModelMaterial& mat : data.materials) {
        GpuMaterial gm{ mat.baseColor, mat.metallic, mat.roughness, textureSetFor(mat.albedoTexture) };
        if (!mat.albedoTextureOriginal.empty() && mat.albedoTexture.empty()) {
            log::get(name())->warn("'{}': material '{}' texture '{}' not found — add its folder to ModelLoadOptions::textureSearchPaths",
                                   path, mat.name, mat.albedoTextureOriginal);
        }
        loaded->materials.push_back(gm);
    }
    for (uint32_t i = 0; i < data.meshes.size(); ++i) {
        const ModelMesh& m = data.meshes[i];
        GpuMesh gm;
        gm.material = m.material;
        gm.skinned = m.skinned;
        gm.meshIndex = i;
        if (!m.skinned) {
            // Vertex color stays white: the tint and material color are
            // applied per draw via the material, see render().
            std::vector<Vertex> verts;
            verts.reserve(m.vertices.size());
            for (const ModelVertex& v : m.vertices) verts.push_back(toVertex(v));
            gm.mesh = std::make_unique<Mesh>(m_app->device(), verts, m.indices);
        }
        loaded->meshes.push_back(std::move(gm));
    }
    ModelId id = m_nextModel++;
    log::get(name())->info("loaded '{}': {} mesh part(s), {} triangles, {} material(s), {} bone(s), {} animation(s), "
                           "bounds ({:.2f},{:.2f},{:.2f})..({:.2f},{:.2f},{:.2f})",
                           path, data.meshes.size(), data.triangleCount(), data.materials.size(), data.bones.size(), data.animations.size(),
                           data.boundsMin.x, data.boundsMin.y, data.boundsMin.z, data.boundsMax.x, data.boundsMax.y, data.boundsMax.z);
    m_models[id] = std::move(loaded);
    m_modelByPath[path] = id;
    return id;
}

const ModelData* ModelModule::model(ModelId id) const {
    auto it = m_models.find(id);
    return it == m_models.end() ? nullptr : &it->second->data;
}

ModelModule::InstanceId ModelModule::spawn(ModelId modelId, const glm::mat4& transform) {
    auto it = m_models.find(modelId);
    if (it == m_models.end()) return 0;
    const LoadedModel& lm = *it->second;
    Instance inst;
    inst.model = modelId;
    inst.transform = transform;
    if (!lm.data.bones.empty()) {
        for (const ModelBone& b : lm.data.bones) inst.locals.push_back(b.localRest);
    }
    for (const GpuMesh& gm : lm.meshes) {
        if (!gm.skinned) continue;
        const ModelMesh& src = lm.data.meshes[gm.meshIndex];
        SkinnedBuffers sb;
        sb.cpu.resize(src.vertices.size());
        for (auto& vb : sb.vertices) {
            vb = std::make_unique<Buffer>(m_app->device(), sizeof(Vertex) * src.vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                          VMA_MEMORY_USAGE_CPU_TO_GPU);
        }
        sb.indices = std::make_unique<Buffer>(Buffer::createDeviceLocal(m_app->device(), src.indices.data(),
                                                                        src.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
        sb.indexCount = static_cast<uint32_t>(src.indices.size());
        inst.skinned.push_back(std::move(sb));
    }
    InstanceId id = m_nextInstance++;
    m_instances[id] = std::move(inst);
    return id;
}

void ModelModule::remove(InstanceId id) { m_instances.erase(id); }

void ModelModule::setTransform(InstanceId id, const glm::mat4& t) {
    if (auto it = m_instances.find(id); it != m_instances.end()) it->second.transform = t;
}

glm::mat4 ModelModule::transform(InstanceId id) const {
    auto it = m_instances.find(id);
    return it == m_instances.end() ? glm::mat4(1.0f) : it->second.transform;
}

void ModelModule::setVisible(InstanceId id, bool visible) {
    if (auto it = m_instances.find(id); it != m_instances.end()) it->second.visible = visible;
}

void ModelModule::setTextureOverride(InstanceId id, const std::string& texturePath) {
    auto it = m_instances.find(id);
    if (it == m_instances.end()) return;
    it->second.textureOverride = texturePath.empty() ? VK_NULL_HANDLE : textureSetFor(texturePath);
    it->second.textureOverridePath = texturePath;
}

std::string ModelModule::textureOverride(InstanceId id) const {
    auto it = m_instances.find(id);
    return it == m_instances.end() ? std::string() : it->second.textureOverridePath;
}

void ModelModule::setWorldOverlay(const std::string& texturePath, float tileSizeMetres, float strength) {
    m_overlaySet = texturePath.empty() ? VK_NULL_HANDLE : textureSetFor(texturePath);
    m_overlayTile = m_overlaySet ? tileSizeMetres : 0.0f;
    m_overlayStrength = strength;
}

void ModelModule::setOverlayEnabled(InstanceId id, bool enabled) {
    if (auto it = m_instances.find(id); it != m_instances.end()) it->second.overlay = enabled;
}

void ModelModule::setTint(InstanceId id, const glm::vec3& tint) {
    if (auto it = m_instances.find(id); it != m_instances.end()) it->second.tint = tint;
}

void ModelModule::playAnimation(InstanceId id, int clip, bool loop, float speed) {
    auto it = m_instances.find(id);
    if (it == m_instances.end()) return;
    const ModelData& data = m_models[it->second.model]->data;
    Instance& inst = it->second;
    inst.clip = (clip >= 0 && clip < static_cast<int>(data.animations.size())) ? clip : -1;
    inst.clipTime = 0.0f;
    inst.clipLoop = loop;
    inst.clipSpeed = speed;
    if (inst.clip < 0) {
        for (size_t b = 0; b < data.bones.size(); ++b) inst.locals[b] = data.bones[b].localRest;
    }
}

void ModelModule::setSkinJiggle(InstanceId id, std::vector<SkinJiggleOffset> zones) {
    if (auto it = m_instances.find(id); it != m_instances.end()) {
        it->second.skinJiggle = std::move(zones);
        it->second.skinnedFrame = ~0ull;
    }
}

std::vector<glm::mat4>* ModelModule::boneLocals(InstanceId id) {
    auto it = m_instances.find(id);
    return it == m_instances.end() || it->second.locals.empty() ? nullptr : &it->second.locals;
}

void ModelModule::setBoneWorldOverride(InstanceId id, std::vector<glm::mat4> world) {
    if (auto it = m_instances.find(id); it != m_instances.end()) it->second.worldOverride = std::move(world);
}

std::vector<glm::mat4> ModelModule::currentBoneWorld(const Instance& inst) const {
    const ModelData& data = m_models.at(inst.model)->data;
    if (inst.worldOverride.size() == data.bones.size()) return inst.worldOverride;
    std::vector<glm::mat4> world(data.bones.size());
    for (size_t b = 0; b < data.bones.size(); ++b) {
        int p = data.bones[b].parent;
        world[b] = p >= 0 ? world[p] * inst.locals[b] : inst.locals[b];
    }
    return world;
}

std::vector<glm::mat4> ModelModule::boneWorld(InstanceId id) const {
    auto it = m_instances.find(id);
    return it == m_instances.end() ? std::vector<glm::mat4>{} : currentBoneWorld(it->second);
}

void ModelModule::update(const UpdateContext& ctx) {
    for (auto& [id, inst] : m_instances) {
        if (inst.clip < 0 || !inst.worldOverride.empty()) continue;
        const ModelAnimation& anim = m_models[inst.model]->data.animations[inst.clip];
        inst.clipTime += ctx.dt * inst.clipSpeed;
        if (inst.clipTime > anim.duration) inst.clipTime = inst.clipLoop && anim.duration > 0.0f ? std::fmod(inst.clipTime, anim.duration) : anim.duration;
        // Linear blend between the two nearest sampled frames.
        float f = inst.clipTime * anim.sampleRate;
        size_t f0 = std::min(static_cast<size_t>(f), anim.frames.size() - 1);
        size_t f1 = std::min(f0 + 1, anim.frames.size() - 1);
        float t = f - static_cast<float>(f0);
        for (size_t b = 0; b < inst.locals.size(); ++b) {
            inst.locals[b] = anim.frames[f0][b] * (1.0f - t) + anim.frames[f1][b] * t;
        }
    }
}

void ModelModule::setDeformedVertices(InstanceId id, const std::vector<std::vector<glm::vec3>>& positions,
                                      const std::vector<std::vector<glm::vec3>>& normals) {
    auto it = m_instances.find(id);
    if (it == m_instances.end()) return;
    Instance& inst = it->second;
    if (positions.empty()) {
        inst.deformed.clear();
        return;
    }
    const LoadedModel& lm = *m_models[inst.model];
    if (inst.deformed.empty()) {
        // First use: CPU mirror + one vertex buffer per frame in flight per
        // mesh part (same scheme as skinned meshes), index buffer shared.
        for (const ModelMesh& src : lm.data.meshes) {
            SkinnedBuffers sb;
            sb.cpu.resize(src.vertices.size());
            for (size_t v = 0; v < src.vertices.size(); ++v) {
                sb.cpu[v] = toVertex(src.vertices[v]);
            }
            for (auto& vb : sb.vertices) {
                vb = std::make_unique<Buffer>(m_app->device(), sizeof(Vertex) * std::max<size_t>(1, src.vertices.size()),
                                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
            }
            sb.indices = std::make_unique<Buffer>(Buffer::createDeviceLocal(m_app->device(), src.indices.data(),
                                                                            std::max<size_t>(1, src.indices.size()) * sizeof(uint32_t),
                                                                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
            sb.indexCount = static_cast<uint32_t>(src.indices.size());
            inst.deformed.push_back(std::move(sb));
        }
    }
    for (size_t m = 0; m < inst.deformed.size() && m < positions.size(); ++m) {
        SkinnedBuffers& sb = inst.deformed[m];
        const size_t n = std::min(sb.cpu.size(), positions[m].size());
        for (size_t v = 0; v < n; ++v) {
            sb.cpu[v].position = positions[m][v];
            if (m < normals.size() && v < normals[m].size()) sb.cpu[v].normal = normals[m][v];
        }
    }
    ++inst.deformVersion;
}

void ModelModule::setDeformedTopology(InstanceId id, const std::vector<std::vector<ModelVertex>>& parts) {
    auto it = m_instances.find(id);
    if (it == m_instances.end()) return;
    Instance& inst = it->second;
    const LoadedModel& lm = *m_models[inst.model];
    inst.deformed.clear();
    const glm::mat4 toModel = glm::inverse(inst.transform);
    for (size_t m = 0; m < lm.data.meshes.size(); ++m) {
        const std::vector<ModelVertex>& src = m < parts.size() ? parts[m] : std::vector<ModelVertex>{};
        SkinnedBuffers sb;
        sb.cpu.reserve(src.size());
        // Parts arrive in world space; the overlay position is the rest
        // position in model space, so the grid stays where it was on the
        // intact object once the parts start moving.
        for (const ModelVertex& v : src) {
            Vertex out = toVertex(v);
            out.color = glm::vec3(toModel * glm::vec4(v.position, 1.0f));
            sb.cpu.push_back(out);
        }
        std::vector<uint32_t> indices(src.size());
        for (uint32_t i = 0; i < indices.size(); ++i) indices[i] = i;
        for (auto& vb : sb.vertices) {
            vb = std::make_unique<Buffer>(m_app->device(), sizeof(Vertex) * std::max<size_t>(1, src.size()), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                          VMA_MEMORY_USAGE_CPU_TO_GPU);
        }
        if (indices.empty()) indices.push_back(0);
        sb.indices = std::make_unique<Buffer>(Buffer::createDeviceLocal(m_app->device(), indices.data(), indices.size() * sizeof(uint32_t),
                                                                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
        sb.indexCount = static_cast<uint32_t>(src.size());
        inst.deformed.push_back(std::move(sb));
    }
    ++inst.deformVersion;
}

bool ModelModule::isDeformed(InstanceId id) const {
    auto it = m_instances.find(id);
    return it != m_instances.end() && !it->second.deformed.empty();
}

void ModelModule::uploadDeformed(Instance& inst, uint32_t frameIndex) {
    if (inst.deformed.empty() || inst.deformUploaded[frameIndex] == inst.deformVersion) return;
    for (SkinnedBuffers& sb : inst.deformed) {
        if (!sb.cpu.empty()) sb.vertices[frameIndex]->upload(sb.cpu.data(), sb.cpu.size() * sizeof(Vertex));
    }
    inst.deformUploaded[frameIndex] = inst.deformVersion;
}

void ModelModule::skinInstance(Instance& inst, uint32_t frameIndex) {
    if (inst.skinnedFrame == m_frame || inst.skinned.empty()) return;
    inst.skinnedFrame = m_frame;
    const LoadedModel& lm = *m_models[inst.model];
    std::vector<glm::mat4> world = currentBoneWorld(inst);
    std::vector<glm::mat4> skin(world.size());
    for (size_t b = 0; b < world.size(); ++b) skin[b] = world[b] * lm.data.bones[b].inverseBind;
    size_t si = 0;
    for (const GpuMesh& gm : lm.meshes) {
        if (!gm.skinned) continue;
        const ModelMesh& src = lm.data.meshes[gm.meshIndex];
        SkinnedBuffers& sb = inst.skinned[si++];
        for (size_t v = 0; v < src.vertices.size(); ++v) {
            glm::vec3 p, n;
            skinVertex(src.vertices[v], skin, p, n);
            if (!inst.skinJiggle.empty()) p += skinJiggleDisplacement(inst.skinJiggle, p);
            sb.cpu[v] = Vertex{ p, src.vertices[v].position, n, src.vertices[v].uv }; // overlay: bind pose
        }
        sb.vertices[frameIndex]->upload(sb.cpu.data(), sb.cpu.size() * sizeof(Vertex));
    }
}

bool ModelModule::mightBeVisible(const Instance& inst, const Frustum& f) const {
    // Deformed parts (breakables) and ragdolls can be anywhere: always drawn.
    if (!inst.deformed.empty() || !inst.worldOverride.empty()) return true;
    const ModelData& d = m_models.at(inst.model)->data;
    glm::vec3 mn = d.boundsMin, mx = d.boundsMax;
    if (!inst.skinned.empty()) {
        // Animation moves limbs past the bind-pose bounds: a margin.
        glm::vec3 pad = (mx - mn) * 0.25f;
        mn -= pad;
        mx += pad;
    }
    glm::vec3 wmn, wmx;
    transformAabb(mn, mx, inst.transform, wmn, wmx);
    return f.intersectsAabb(wmn, wmx);
}

// Groups visible, rigid (not skinned, not deformed) instances of the same
// model, texture and overlay setting. Groups of 2+ are drawn instanced;
// singles stay on the normal path (no instance buffer for one copy).
void ModelModule::buildBatches(const Frustum& f, std::vector<Batch>& out, std::vector<InstanceGpu>& data, bool markDrawn) {
    out.clear();
    data.clear();
    if (!m_instancing) return;
    struct Key {
        ModelId model; VkDescriptorSet tex; bool overlay;
        bool operator<(const Key& o) const { return std::tie(model, tex, overlay) < std::tie(o.model, o.tex, o.overlay); }
    };
    std::map<Key, std::vector<Instance*>> groups;
    for (auto& [id, inst] : m_instances) {
        if (!inst.visible || !inst.deformed.empty() || !inst.worldOverride.empty() || !inst.skinned.empty()) continue;
        const LoadedModel& lm = *m_models[inst.model];
        bool anySkinned = false;
        for (const GpuMesh& gm : lm.meshes) anySkinned |= gm.skinned;
        if (anySkinned || !mightBeVisible(inst, f)) continue;
        groups[{ inst.model, inst.textureOverride, inst.overlay }].push_back(&inst);
    }
    for (auto& [key, list] : groups) {
        if (list.size() < 2) continue;
        Batch b{ key.model, key.tex, key.overlay, static_cast<uint32_t>(data.size()), static_cast<uint32_t>(list.size()) };
        for (Instance* inst : list) {
            data.push_back({ inst->transform, glm::vec4(inst->tint, 1.0f) });
            if (markDrawn) inst->batchedFrame = m_frame;
        }
        out.push_back(b);
    }
}

void ModelModule::uploadInstances(int pass, uint32_t frameIndex, const std::vector<InstanceGpu>& data) {
    if (data.empty()) return;
    auto& buf = m_instanceBuffers[pass][frameIndex];
    size_t& cap = m_instanceCapacity[pass][frameIndex];
    if (!buf || cap < data.size()) {
        cap = std::max<size_t>(64, data.size() * 2);
        buf = std::make_unique<Buffer>(m_app->device(), sizeof(InstanceGpu) * cap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    buf->upload(data.data(), sizeof(InstanceGpu) * data.size());
}

void ModelModule::renderShadow(const ShadowRenderContext& ctx) {
    ++m_frame; // renderShadow runs first each frame (see Application's frame loop)
    m_shadowPipeline->bind(ctx.cmd);
    // Cull against the light's own frustum: things off camera still cast
    // shadows into view, things outside the shadow map can't.
    const Frustum lightFrustum = Frustum::fromViewProj(ctx.lightViewProj);
    // Instanced groups first; what they drew is skipped below.
    if (m_showMeshes) {
        buildBatches(lightFrustum, m_batches, m_instanceData, false);
        uploadInstances(0, ctx.frameIndex, m_instanceData);
        if (!m_batches.empty()) {
            m_instancedShadowPipeline->bind(ctx.cmd);
            ShadowPushConstants pc{ ctx.lightViewProj, glm::mat4(1.0f) };
            vkCmdPushConstants(ctx.cmd, m_instancedShadowPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
            VkBuffer ib = m_instanceBuffers[0][ctx.frameIndex]->handle();
            for (const Batch& b : m_batches) {
                const LoadedModel& lm = *m_models[b.model];
                VkDeviceSize off = sizeof(InstanceGpu) * b.first;
                vkCmdBindVertexBuffers(ctx.cmd, 1, 1, &ib, &off);
                for (const GpuMesh& gm : lm.meshes) {
                    gm.mesh->bind(ctx.cmd);
                    gm.mesh->drawInstanced(ctx.cmd, b.count);
                }
            }
            m_shadowPipeline->bind(ctx.cmd);
        }
    }
    // Which instances the batches covered (same rules as buildBatches).
    auto batched = [&](const Instance& inst) {
        if (m_batches.empty() || !inst.deformed.empty() || !inst.worldOverride.empty() || !inst.skinned.empty()) return false;
        for (const Batch& b : m_batches)
            if (b.model == inst.model && b.textureOverride == inst.textureOverride && b.overlay == inst.overlay) return true;
        return false;
    };
    for (auto& [id, inst] : m_instances) {
        if (!inst.visible || !m_showMeshes) continue;
        if (!mightBeVisible(inst, lightFrustum)) continue;
        if (batched(inst)) continue;
        skinInstance(inst, ctx.frameIndex);
        if (!inst.deformed.empty()) {
            uploadDeformed(inst, ctx.frameIndex);
            ShadowPushConstants pc{ ctx.lightViewProj, glm::mat4(1.0f) }; // vertices are already in world space
            vkCmdPushConstants(ctx.cmd, m_shadowPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
            for (SkinnedBuffers& sb : inst.deformed) {
                if (!sb.indexCount) continue;
                VkBuffer vb = sb.vertices[ctx.frameIndex]->handle();
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
                vkCmdBindIndexBuffer(ctx.cmd, sb.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(ctx.cmd, sb.indexCount, 1, 0, 0, 0);
            }
            continue;
        }
        ShadowPushConstants pc{ ctx.lightViewProj, inst.transform };
        vkCmdPushConstants(ctx.cmd, m_shadowPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        const LoadedModel& lm = *m_models[inst.model];
        size_t si = 0;
        for (const GpuMesh& gm : lm.meshes) {
            if (gm.skinned) {
                SkinnedBuffers& sb = inst.skinned[si++];
                VkBuffer vb = sb.vertices[ctx.frameIndex]->handle();
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
                vkCmdBindIndexBuffer(ctx.cmd, sb.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(ctx.cmd, sb.indexCount, 1, 0, 0, 0);
            } else {
                gm.mesh->bind(ctx.cmd);
                gm.mesh->draw(ctx.cmd);
            }
        }
    }
}

void ModelModule::render(const RenderContext& ctx) {
    m_drawCalls = 0;
    m_culled = 0;
    // Frustum culling (OPTIMIZATION.md #23): an instance entirely outside
    // the view is neither skinned, uploaded nor drawn.
    const Frustum frustum = Frustum::fromViewProj(ctx.proj * ctx.view);
    VkDescriptorSet overlay = m_overlaySet ? m_overlaySet : ctx.defaultMaterialTextureDescriptorSet;
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, ctx.defaultMaterialTextureDescriptorSet, overlay };
    const VkShaderStageFlags pcStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    m_instancedCount = 0;
    if (m_showMeshes) {
        // Instancing (OPTIMIZATION.md #25): 2+ visible copies of the same
        // rigid model (same texture/overlay) are one draw per mesh part.
        buildBatches(frustum, m_batches, m_instanceData, true);
        uploadInstances(1, ctx.frameIndex, m_instanceData);
        if (!m_batches.empty()) {
            m_instancedPipeline->bind(ctx.cmd);
            vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_instancedPipeline->layout(), 0, 4, sets, 0, nullptr);
            VkBuffer ib = m_instanceBuffers[1][ctx.frameIndex]->handle();
            VkDescriptorSet bound = ctx.defaultMaterialTextureDescriptorSet;
            for (const Batch& b : m_batches) {
                const LoadedModel& lm = *m_models[b.model];
                VkDeviceSize off = sizeof(InstanceGpu) * b.first;
                vkCmdBindVertexBuffers(ctx.cmd, 1, 1, &ib, &off);
                for (const GpuMesh& gm : lm.meshes) {
                    const GpuMaterial& mat = lm.materials[gm.material];
                    VkDescriptorSet tex = mat.textureSet ? mat.textureSet : ctx.defaultMaterialTextureDescriptorSet;
                    if (b.textureOverride && mat.textureSet) tex = b.textureOverride;
                    if (tex != bound) {
                        vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_instancedPipeline->layout(), 2, 1, &tex, 0, nullptr);
                        bound = tex;
                    }
                    PushConstants pc{ glm::mat4(1.0f), glm::vec4(mat.metallic, mat.roughness, b.overlay ? m_overlayTile : 0.0f, m_overlayStrength),
                                      glm::vec4(mat.color, 0.0f) };
                    vkCmdPushConstants(ctx.cmd, m_instancedPipeline->layout(), pcStages, 0, sizeof(pc), &pc);
                    gm.mesh->bind(ctx.cmd);
                    gm.mesh->drawInstanced(ctx.cmd, b.count);
                    ++m_drawCalls;
                }
                m_instancedCount += b.count;
            }
        }
        m_pipeline->bind(ctx.cmd);
        vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(), 0, 4, sets, 0, nullptr);
        VkDescriptorSet boundTexture = ctx.defaultMaterialTextureDescriptorSet;
        for (auto& [id, inst] : m_instances) {
            if (!inst.visible) continue;
            if (!mightBeVisible(inst, frustum)) { ++m_culled; continue; }
            if (inst.batchedFrame == m_frame) continue; // drawn instanced above
            skinInstance(inst, ctx.frameIndex);
            uploadDeformed(inst, ctx.frameIndex);
            const LoadedModel& lm = *m_models[inst.model];
            const bool deformed = !inst.deformed.empty();
            size_t si = 0;
            for (const GpuMesh& gm : lm.meshes) {
                const GpuMaterial& mat = lm.materials[gm.material];
                // A texture variant (e.g. Synty's _Texture_02) replaces the
                // atlas of every textured material; untextured ones keep
                // their plain color.
                VkDescriptorSet tex = mat.textureSet ? mat.textureSet : ctx.defaultMaterialTextureDescriptorSet;
                if (inst.textureOverride && mat.textureSet) tex = inst.textureOverride;
                if (tex != boundTexture) {
                    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(), 2, 1, &tex, 0, nullptr);
                    boundTexture = tex;
                }
                float tile = inst.overlay ? m_overlayTile : 0.0f;
                // Deformed parts are already in world space (identity
                // model matrix) but their overlay positions are in model
                // space: overlayScale carries the object's scale for them.
                PushConstants pc{ deformed ? glm::mat4(1.0f) : inst.transform, glm::vec4(mat.metallic, mat.roughness, tile, m_overlayStrength),
                                  glm::vec4(mat.color * inst.tint, deformed ? instanceScale(inst.transform) : 0.0f) };
                vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), pcStages, 0, sizeof(pc), &pc);
                if (deformed) {
                    SkinnedBuffers& sb = inst.deformed[gm.meshIndex];
                    if (sb.indexCount) {
                        VkBuffer vb = sb.vertices[ctx.frameIndex]->handle();
                        VkDeviceSize off = 0;
                        vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
                        vkCmdBindIndexBuffer(ctx.cmd, sb.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(ctx.cmd, sb.indexCount, 1, 0, 0, 0);
                    }
                } else if (gm.skinned) {
                    SkinnedBuffers& sb = inst.skinned[si++];
                    VkBuffer vb = sb.vertices[ctx.frameIndex]->handle();
                    VkDeviceSize off = 0;
                    vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vb, &off);
                    vkCmdBindIndexBuffer(ctx.cmd, sb.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(ctx.cmd, sb.indexCount, 1, 0, 0, 0);
                } else {
                    gm.mesh->bind(ctx.cmd);
                    gm.mesh->draw(ctx.cmd);
                }
                ++m_drawCalls;
            }
        }
    }

    if (!m_showBones) return;
    m_bonePipeline->bind(ctx.cmd);
    sets[3] = ctx.defaultMaterialTextureDescriptorSet;
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bonePipeline->layout(), 0, 4, sets, 0, nullptr);
    m_boneMesh->bind(ctx.cmd);
    for (auto& [id, inst] : m_instances) {
        if (!inst.visible || inst.locals.empty()) continue;
        const ModelData& data = m_models[inst.model]->data;
        std::vector<glm::mat4> world = currentBoneWorld(inst);
        for (size_t b = 0; b < data.bones.size(); ++b) {
            int p = data.bones[b].parent;
            if (p < 0) continue;
            glm::vec3 a = glm::vec3(inst.transform * world[p][3]);
            glm::vec3 c = glm::vec3(inst.transform * world[b][3]);
            glm::vec3 d = c - a;
            float len = glm::length(d);
            if (len < 1e-4f) continue;
            glm::vec3 y = d / len;
            glm::vec3 x = glm::normalize(glm::cross(std::abs(y.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), y));
            glm::vec3 z = glm::cross(x, y);
            float thick = std::clamp(len * 0.12f, 0.006f, 0.03f);
            glm::mat4 m(glm::vec4(x * thick, 0), glm::vec4(y * len, 0), glm::vec4(z * thick, 0), glm::vec4(a, 1));
            PushConstants pc{ m, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), glm::vec4(1.0f, 0.82f, 0.25f, 0.0f) };
            vkCmdPushConstants(ctx.cmd, m_bonePipeline->layout(), pcStages, 0, sizeof(pc), &pc);
            m_boneMesh->draw(ctx.cmd);
        }
    }
}

void ModelModule::shutdown() {
    m_instances.clear();
    m_models.clear();
}

} // namespace kke
