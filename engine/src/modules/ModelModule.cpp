#include "kke/modules/ModelModule.h"
#include "kke/DrawOrder.h"

#include "kke/Application.h"
#include "kke/GpuSkin.h"
#include "kke/InteriorColor.h"
#include "kke/MeshLod.h"
#include "kke/Occlusion.h"
#include "kke/Log.h"
#include "kke/Picking.h"
#include "kke/RayScene.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"
#include "kke/rhi/VulkanCommandList.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <iterator>

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

// A few threads that wait for a batch of independent jobs (one skinned
// instance each) and run them alongside the calling thread.
class SkinWorkers {
public:
    explicit SkinWorkers(int threads) {
        for (int i = 0; i < threads; ++i) m_threads.emplace_back([this] { loop(); });
    }
    ~SkinWorkers() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_quit = true;
        }
        m_wake.notify_all();
        for (std::thread& t : m_threads) t.join();
    }
    size_t threads() const { return m_threads.size(); }
    // fn(0 .. count-1), spread over the workers and this thread; returns
    // when all are done.
    void run(size_t count, const std::function<void(size_t)>& fn) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_fn = &fn;
            m_count = count;
            m_next.store(0);
            m_busy = m_threads.size();
            ++m_generation;
        }
        m_wake.notify_all();
        work();
        std::unique_lock<std::mutex> lock(m_mutex);
        m_done.wait(lock, [this] { return m_busy == 0; });
        m_fn = nullptr;
    }

private:
    void work() {
        for (size_t i = m_next.fetch_add(1); i < m_count; i = m_next.fetch_add(1)) (*m_fn)(i);
    }
    void loop() {
        uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait(lock, [&] { return m_quit || m_generation != seen; });
                if (m_quit) return;
                seen = m_generation;
            }
            work();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                --m_busy;
            }
            m_done.notify_one();
        }
    }

    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_wake, m_done;
    const std::function<void(size_t)>* m_fn = nullptr;
    size_t m_count = 0;
    std::atomic<size_t> m_next{ 0 };
    size_t m_busy = 0;
    uint64_t m_generation = 0;
    bool m_quit = false;
};

ModelModule::GpuSkinState::~GpuSkinState() {
    if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
}

ModelModule::ModelModule() = default;
ModelModule::~ModelModule() = default;

void ModelModule::init(Application& app) {
    m_app = &app;
    m_device = app.device().device();
    // ENG-G03: set 2 = albedo, normal, roughness and metallic maps (model.frag).
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (uint32_t i = 0; i < 4; ++i) bindings[i] = { i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_surfaceLayout));
    constexpr uint32_t kMaxSurfaceSets = 1024;
    VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * kMaxSurfaceSets };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kMaxSurfaceSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &size;
    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_surfacePool));
    const uint8_t flat[4] = { 128, 128, 255, 0 };
    m_flatNormal = std::make_unique<Texture>(app.device(), flat, 1, 1, Texture::LinearData{});
    m_defaultSurface = surfaceSetFor("", "", "", "");
    {
        // Skinning threads: the governor's worker budget (which counts this
        // thread), never more than the machine has.
        const int hw = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        const int extra = std::min(app.resourceBudget().workerThreads, hw) - 1;
        if (extra > 0) m_skinWorkers = std::make_unique<SkinWorkers>(std::min(extra, 15));
    }
    {
        // ENG-G08: skinning on the GPU (shaders/skin.comp) unless
        // KKE_CPU_SKINNING=1 (the CPU path, for comparisons).
        const char* cpuSkinning = std::getenv("KKE_CPU_SKINNING");
        m_gpuSkinning = !(cpuSkinning && std::string(cpuSkinning) == "1");
        if (m_gpuSkinning) createSkinPipeline();
    }

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
    // Set 2 = the material's maps (surfaceSetFor), set 3 = world overlay texture.
    config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), m_surfaceLayout,
                                    app.materialTextureSetLayout() };
    m_pipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/model.vert.spv", "shaders/model.frag.spv", config);

    // Skeleton overlay: same shader, drawn on top of everything.
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTestEnable = false;
    config.depthWriteEnable = false;
    m_bonePipeline = std::make_unique<Pipeline>(app.device(), app.renderer().renderPass(), "shaders/model.vert.spv", "shaders/model.frag.spv", config);

    PipelineConfig shadowConfig = ShadowMap::casterConfig();
    shadowConfig.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants) };
    m_shadowPipeline = std::make_unique<Pipeline>(app.device(), app.shadowMap().renderPass(), "shaders/shadow.vert.spv", "shaders/shadow.frag.spv", shadowConfig);

    // Instanced variants (docs/OPTIMIZATION.md #25): kke::Vertex at binding 0,
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
        inst.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), m_surfaceLayout,
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

VkDescriptorSet ModelModule::surfaceSetFor(const std::string& albedo, const std::string& normal, const std::string& roughness,
                                          const std::string& metallic) {
    const std::string key = albedo + '\n' + normal + '\n' + roughness + '\n' + metallic;
    if (auto it = m_surfaceSets.find(key); it != m_surfaceSets.end()) return it->second;

    const Texture& white = m_app->defaultWhiteTexture();
    const Texture* maps[4] = { &white, m_flatNormal.get(), &white, &white };
    const std::string* paths[4] = { &albedo, &normal, &roughness, &metallic };

    for (uint32_t i = 0; i < 4; ++i) {
        if (!paths[i]->empty()) {
            if (const Texture* t = m_app->cachedTexture(*paths[i], i != 0)) maps[i] = t;
        }
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = m_surfacePool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_surfaceLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &alloc, &set) != VK_SUCCESS) {
        log::get(name())->warn("material set pool full; '{}' drawn with default maps", albedo);
        return m_defaultSurface;
    }

    VkDescriptorImageInfo images[4];
    VkWriteDescriptorSet writes[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        images[i] = { maps[i]->sampler(), maps[i]->imageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(m_device, 4, writes, 0, nullptr);

    m_surfaceSets[key] = set;
    return set;
}

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

ModelModule::ModelId ModelModule::add(ModelData modelData, const std::string& key, bool transient) {
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
        GpuMaterial gm{ mat.baseColor, mat.metallic, mat.roughness,
                        surfaceSetFor(mat.albedoTexture, mat.normalTexture, mat.roughnessTexture, mat.metallicTexture),
                        !mat.albedoTexture.empty() };
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
            gm.mesh = std::make_unique<Mesh>(m_app->device(), verts, m.indices, transient ? Mesh::Memory::HostVisible : Mesh::Memory::DeviceLocal);
        }
        if (m.skinned && m_gpuSkinning && !m.vertices.empty()) {
            // ENG-G08: the bind vertices skin.comp reads, uploaded once.
            const std::vector<glm::vec4> bind = packSkinBind(m.vertices);
            gm.skinBind = std::make_unique<Buffer>(
                Buffer::createDeviceLocal(m_app->device(), bind.data(), bind.size() * sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        }
        loaded->meshes.push_back(std::move(gm));
    }
    ModelId id = m_nextModel++;
    if (!transient) log::get(name())->info("loaded '{}': {} mesh part(s), {} triangles, {} material(s), {} bone(s), {} animation(s), "
                           "bounds ({:.2f},{:.2f},{:.2f})..({:.2f},{:.2f},{:.2f})",
                           path, data.meshes.size(), data.triangleCount(), data.materials.size(), data.bones.size(), data.animations.size(),
                           data.boundsMin.x, data.boundsMin.y, data.boundsMin.z, data.boundsMax.x, data.boundsMax.y, data.boundsMax.z);
    m_models[id] = std::move(loaded);
    m_modelByPath[path] = id;
    return id;
}

bool ModelModule::unload(ModelId id) {
    auto it = m_models.find(id);
    if (it == m_models.end()) return false;
    for (const auto& [iid, inst] : m_instances)
        if (inst.model == id) return false;
    // A command buffer still in flight may draw these buffers: the
    // renderer frees them once those frames are done.
    for (GpuMesh& gm : it->second->meshes)
        if (gm.mesh) m_app->renderer().retire(std::move(gm.mesh));
    for (auto p = m_modelByPath.begin(); p != m_modelByPath.end();) p = p->second == id ? m_modelByPath.erase(p) : std::next(p);
    m_models.erase(it);
    return true;
}

void ModelModule::drawStandalone(VkCommandBuffer cmd, ModelId id, const glm::mat4& transform, VkDescriptorSet lighting, VkDescriptorSet shadowMap,
                                 VkDescriptorSet defaultTexture) {
    auto it = m_models.find(id);
    if (it == m_models.end()) return;
    LoadedModel& lm = *it->second;
    VkDescriptorSet sets[] = { lighting, shadowMap, m_defaultSurface, defaultTexture };
    m_pipeline->bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(), 0, 4, sets, 0, nullptr);
    const VkShaderStageFlags pcStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSet bound = m_defaultSurface;
    for (GpuMesh& gm : lm.meshes) {
        if (!gm.mesh) {
            // Skinned parts are drawn from per-instance skinned copies;
            // standalone they need their rest pose once. Skinned, not the
            // raw vertices: those are in the mesh's own space, which is
            // only the model's in some packs (Quaternius' animals are
            // turned and 100x apart, and would miss the thumbnail).
            const ModelMesh& m = lm.data.meshes[gm.meshIndex];
            if (m.vertices.empty() || m.indices.empty()) continue;
            std::vector<glm::mat4> skin;
            if (!lm.data.bones.empty()) {
                std::vector<glm::mat4> locals(lm.data.bones.size());
                for (size_t b = 0; b < locals.size(); ++b) locals[b] = lm.data.bones[b].localRest;
                skin = computeSkinMatrices(lm.data, locals);
            }
            std::vector<Vertex> verts;
            verts.reserve(m.vertices.size());
            for (const ModelVertex& v : m.vertices) {
                ModelVertex posed = v;
                if (!skin.empty() && v.weights != glm::vec4(0.0f)) skinVertex(v, skin, posed.position, posed.normal);
                verts.push_back(toVertex(posed));
            }
            gm.mesh = std::make_unique<Mesh>(m_app->device(), verts, m.indices, Mesh::Memory::HostVisible);
        }
        const GpuMaterial& mat = lm.materials[gm.material];
        VkDescriptorSet tex = mat.textureSet ? mat.textureSet : m_defaultSurface;
        if (tex != bound) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(), 2, 1, &tex, 0, nullptr);
            bound = tex;
        }
        PushConstants pc{ transform, glm::vec4(mat.metallic, mat.roughness, 0.0f, 0.0f), glm::vec4(mat.color, 0.0f) };
        vkCmdPushConstants(cmd, m_pipeline->layout(), pcStages, 0, sizeof(pc), &pc);
        gm.mesh->bind(cmd);
        gm.mesh->draw(cmd);
    }
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
        // GPU skinning: skin.comp writes these (device memory); the CPU path
        // writes them from sb.cpu.
        if (!m_gpuSkinning) sb.cpu.resize(src.vertices.size());
        // With ray query they're also what classic ray tracing traces (RayScene::addDynamic).
        const VkBufferUsageFlags rays = m_app->device().rayQuery() ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                                                   : 0;
        const VkBufferUsageFlags usage =
            (m_gpuSkinning ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) | rays;
        for (auto& vb : sb.vertices) {
            vb = std::make_unique<Buffer>(m_app->device(), sizeof(Vertex) * std::max<size_t>(1, src.vertices.size()), usage,
                                          m_gpuSkinning ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_CPU_TO_GPU);
        }
        sb.indices = std::make_unique<Buffer>(Buffer::createDeviceLocal(m_app->device(), src.indices.data(),
                                                                        src.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rays));
        sb.indexCount = static_cast<uint32_t>(src.indices.size());
        inst.skinned.push_back(std::move(sb));
    }
    if (m_gpuSkinning && !inst.skinned.empty()) createGpuSkin(inst);
    InstanceId id = m_nextInstance++;
    m_instances[id] = std::move(inst);
    return id;
}

void ModelModule::remove(InstanceId id) {
    const auto it = m_instances.find(id);
    if (it == m_instances.end()) return;
    // Frames still in flight may draw its skinned vertex buffers: they go
    // once those frames are done (removing a character mid-game).
    Instance& inst = it->second;
    if (m_app && (!inst.skinned.empty() || !inst.deformed.empty())) {
        auto buffers = std::make_shared<std::vector<SkinnedBuffers>>(std::move(inst.skinned));
        for (SkinnedBuffers& b : inst.deformed) buffers->push_back(std::move(b));
        m_app->renderer().retire(std::shared_ptr<void>(std::move(buffers)));
        if (inst.gpuSkin) m_app->renderer().retire(std::shared_ptr<void>(std::move(inst.gpuSkin))); // its sets, same frames
    }
    m_instances.erase(it);
}

void ModelModule::setTransform(InstanceId id, const glm::mat4& t) {
    if (auto it = m_instances.find(id); it != m_instances.end()) it->second.transform = t;
}

glm::mat4 ModelModule::transform(InstanceId id) const {
    auto it = m_instances.find(id);
    return it == m_instances.end() ? glm::mat4(1.0f) : it->second.transform;
}

ModelModule::ModelId ModelModule::instanceModel(InstanceId id) const {
    auto it = m_instances.find(id);
    return it == m_instances.end() ? 0 : it->second.model;
}

void ModelModule::setVisible(InstanceId id, bool visible) {
    if (auto it = m_instances.find(id); it != m_instances.end()) it->second.visible = visible;
}

void ModelModule::setTextureOverride(InstanceId id, const std::string& texturePath) {
    auto it = m_instances.find(id);
    if (it == m_instances.end()) return;
    it->second.textureOverride = texturePath.empty() ? VK_NULL_HANDLE : surfaceSetFor(texturePath, "", "", "");
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

void ModelModule::setPoseModifier(InstanceId id, PoseModifier modifier) {
    auto it = m_instances.find(id);
    if (it == m_instances.end()) return;
    it->second.modifier = std::move(modifier);
    it->second.modified.clear();
}

std::vector<glm::mat4> ModelModule::currentBoneWorld(const Instance& inst) const {
    const ModelData& data = m_models.at(inst.model)->data;
    if (inst.worldOverride.size() == data.bones.size()) return inst.worldOverride;
    if (inst.modified.size() == data.bones.size()) return inst.modified;
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
    advanceClips(ctx);
    // Procedural layers on top of whatever the clip left.
    for (auto& [id, inst] : m_instances) {
        if (!inst.modifier || !inst.worldOverride.empty() || inst.locals.empty()) continue;
        inst.modified.clear();
        std::vector<glm::mat4> world = currentBoneWorld(inst);
        inst.modifier(world, ctx.dt);
        inst.modified = std::move(world);
    }
}

void ModelModule::advanceClips(const UpdateContext& ctx) {
    // Clips keep playing under a world override: a stand-up blends the
    // override from the ragdoll pose to boneLocals(), which has to move.
    for (auto& [id, inst] : m_instances) {
        if (inst.clip < 0) continue;
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
                                      const std::vector<std::vector<glm::vec3>>& normals, bool modelSpace) {
    auto it = m_instances.find(id);
    if (it == m_instances.end()) return;
    Instance& inst = it->second;
    if (positions.empty()) {
        // Back to the model's own mesh: frames in flight may still draw the
        // deformed buffers, so they go once those frames are done.
        if (m_app && !inst.deformed.empty())
            m_app->renderer().retire(std::shared_ptr<void>(std::make_shared<std::vector<SkinnedBuffers>>(std::move(inst.deformed))));
        inst.deformed.clear();
        inst.deformedInModelSpace = false;
        return;
    }
    inst.deformedInModelSpace = modelSpace;
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

// skin.comp's descriptor set layout (bind vertices, skin data, the part's
// vertices: three storage buffers), its pipeline layout and pipeline.
void ModelModule::createSkinPipeline() {
    VkDescriptorSetLayoutBinding bindings[3]{};
    for (uint32_t i = 0; i < 3; ++i) bindings[i] = { i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_skinSetLayout));
    const VkPushConstantRange push{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SkinPush) };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_skinSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push;
    VK_CHECK(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_skinLayout));
    VkShaderModule module = loadShaderModule(m_app->device(), "shaders/skin.comp.spv");
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = "main";
    info.layout = m_skinLayout;
    VK_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &m_skinPipeline));
    vkDestroyShaderModule(m_device, module, nullptr);
}

// An instance's own descriptor pool, with one skin.comp set per skinned
// part and frame slot. The sets point at the buffers once packSkin has
// made the slot's data buffer (writeSkinSets).
void ModelModule::createGpuSkin(Instance& inst) {
    const uint32_t sets = static_cast<uint32_t>(inst.skinned.size()) * Renderer::kMaxFramesInFlight;
    auto state = std::make_shared<GpuSkinState>();
    state->device = m_device;
    const VkDescriptorPoolSize size{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * sets };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = sets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &size;
    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &state->pool));
    for (SkinnedBuffers& sb : inst.skinned) {
        for (VkDescriptorSet& set : sb.skinSets) {
            VkDescriptorSetAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc.descriptorPool = state->pool;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &m_skinSetLayout;
            VK_CHECK(vkAllocateDescriptorSets(m_device, &alloc, &set));
        }
    }
    inst.gpuSkin = std::move(state);
}

// Points frame slot `slot`'s sets at the part's bind vertices, the slot's
// skin data and the part's vertex buffer for that slot. Safe while
// recording that slot: its previous frame has finished.
void ModelModule::writeSkinSets(Instance& inst, uint32_t slot) {
    const LoadedModel& lm = *m_models.at(inst.model);
    size_t si = 0;
    for (const GpuMesh& gm : lm.meshes) {
        if (!gm.skinned) continue;
        SkinnedBuffers& sb = inst.skinned[si++];
        if (!gm.skinBind) continue;
        const VkDescriptorBufferInfo infos[3] = { { gm.skinBind->handle(), 0, VK_WHOLE_SIZE },
                                                  { inst.gpuSkin->data[slot]->handle(), 0, VK_WHOLE_SIZE },
                                                  { sb.vertices[slot]->handle(), 0, VK_WHOLE_SIZE } };
        VkWriteDescriptorSet writes[3]{};
        for (uint32_t b = 0; b < 3; ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = sb.skinSets[slot];
            writes[b].dstBinding = b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[b].pBufferInfo = &infos[b];
        }
        vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
    }
}

// The CPU half of GPU skinning, for one instance (runs on the skinning
// threads): the skin matrices as skinInstance makes them, and the jiggle
// zones, into frame slot `slot`'s data buffer.
void ModelModule::packSkin(Instance& inst, uint32_t slot) {
    const LoadedModel& lm = *m_models.at(inst.model); // no operator[]: this runs on several threads at once
    const std::vector<glm::mat4> world = currentBoneWorld(inst);
    std::vector<glm::mat4> skin(world.size());
    for (size_t b = 0; b < world.size(); ++b) skin[b] = world[b] * lm.data.bones[b].inverseBind;
    GpuSkinState& state = *inst.gpuSkin;
    packSkinData(skin, inst.skinJiggle, state.packed);
    if (state.capacity[slot] < state.packed.size() || !state.data[slot]) {
        state.capacity[slot] = std::max<size_t>(state.packed.size(), kSkinVec4sPerBone);
        state.data[slot] = std::make_unique<Buffer>(m_app->device(), state.capacity[slot] * sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                    VMA_MEMORY_USAGE_CPU_TO_GPU);
        writeSkinSets(inst, slot);
    }
    if (!state.packed.empty()) state.data[slot]->upload(state.packed.data(), state.packed.size() * sizeof(glm::vec4));
}

// One skin.comp dispatch per skinned part of an instance (the pipeline is
// bound by the caller).
void ModelModule::recordSkin(VkCommandBuffer cmd, Instance& inst, uint32_t slot) {
    const LoadedModel& lm = *m_models.at(inst.model);
    const SkinPush base{ 0u, static_cast<uint32_t>(lm.data.bones.size()), static_cast<uint32_t>(inst.skinJiggle.size()), 0u };
    size_t si = 0;
    for (const GpuMesh& gm : lm.meshes) {
        if (!gm.skinned) continue;
        SkinnedBuffers& sb = inst.skinned[si++];
        if (!gm.skinBind) continue;
        SkinPush pc = base;
        pc.vertexCount = static_cast<uint32_t>(lm.data.meshes[gm.meshIndex].vertices.size());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_skinLayout, 0, 1, &sb.skinSets[slot], 0, nullptr);
        vkCmdPushConstants(cmd, m_skinLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (pc.vertexCount + kSkinGroupSize - 1) / kSkinGroupSize, 1, 1);
    }
}

// GPU skinning, every drawn frame (paused too, so both frame slots hold
// the pose being drawn): the bone matrices of every visible skinned
// instance on the skinning threads, then one dispatch per part. No frustum
// test: the views' frustums aren't known yet and the GPU's share is small.
void ModelModule::computeEveryFrame(VkCommandBuffer cmd) {
    if (!m_gpuSkinning || !m_showMeshes) return;
    const uint32_t slot = m_app->renderer().currentFrameIndex();
    m_toSkin.clear();
    for (auto& [id, inst] : m_instances)
        if (inst.visible && inst.gpuSkin && inst.deformed.empty()) m_toSkin.push_back(&inst);
    if (m_toSkin.empty()) return;
    if (!m_skinWorkers || m_toSkin.size() < 4) {
        for (Instance* inst : m_toSkin) packSkin(*inst, slot);
    } else {
        const std::function<void(size_t)> job = [this, slot](size_t i) { packSkin(*m_toSkin[i], slot); };
        m_skinWorkers->run(m_toSkin.size(), job);
    }
    // This slot's previous frame drew from these vertex buffers (write after
    // read: execution is enough) and wrote them here (write after write: that
    // write must be made available too, or sync validation flags it).
    VkMemoryBarrier rewrite{};
    rewrite.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    rewrite.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    rewrite.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &rewrite, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_skinPipeline);
    for (Instance* inst : m_toSkin) recordSkin(cmd, *inst, slot);
    // The shadow, main and pick passes read the vertices as vertex input.
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void ModelModule::skinInstance(Instance& inst, uint32_t frameIndex) {
    if (m_gpuSkinning || inst.skinnedFrame == m_frame || inst.skinned.empty()) return; // GPU skinning: computeEveryFrame did it
    inst.skinnedFrame = m_frame;
    const LoadedModel& lm = *m_models.at(inst.model); // no operator[]: this runs on several threads at once
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

void ModelModule::skinVisible(const Frustum& f, uint32_t frameIndex) {
    if (m_gpuSkinning) return; // computeEveryFrame skinned every visible instance
    m_toSkin.clear();
    for (auto& [id, inst] : m_instances)
        if (inst.visible && !inst.skinned.empty() && inst.skinnedFrame != m_frame && mightBeVisible(inst, f)) m_toSkin.push_back(&inst);
    if (!m_skinWorkers || m_toSkin.size() < 4) {
        for (Instance* inst : m_toSkin) skinInstance(*inst, frameIndex);
        return;
    }
    const std::function<void(size_t)> job = [this, frameIndex](size_t i) { skinInstance(*m_toSkin[i], frameIndex); };
    m_skinWorkers->run(m_toSkin.size(), job);
}

bool ModelModule::mightBeVisible(const Instance& inst, const Frustum& f) const {
    // Deformed parts (breakables) and ragdolls can be anywhere: always drawn.
    if ((!inst.deformed.empty() && !inst.deformedInModelSpace) || !inst.worldOverride.empty()) return true;
    const ModelData& d = m_models.at(inst.model)->data;
    glm::vec3 mn = d.boundsMin, mx = d.boundsMax;
    if (!inst.skinned.empty() || !inst.deformed.empty()) {
        // Animation moves limbs past the bind-pose bounds: a margin.
        glm::vec3 pad = (mx - mn) * 0.25f;
        mn -= pad;
        mx += pad;
    }
    glm::vec3 wmn, wmx;
    transformAabb(mn, mx, inst.transform, wmn, wmx);
    if (!f.intersectsAabb(wmn, wmx)) return false;
    // ENG-F13: hidden behind what was on screen two frames ago (the camera's view only), worked out once per
    // instance and draw: skinning, batching and the single draws all ask.
    if (m_occlusion) {
        if (inst.occlusionPass != m_viewPass) {
            inst.occlusionPass = m_viewPass;
            inst.occluded = occludedBy(*m_occlusion, wmn, wmx);
        }
        if (inst.occluded) {
            if (m_countOccluded) ++m_occluded;
            return false;
        }
    }
    return true;
}

// Groups visible, rigid (not skinned, not deformed) instances of the same
// model, texture and overlay setting. Groups of 2+ are drawn instanced;
// singles stay on the normal path (no instance buffer for one copy).
void ModelModule::buildBatches(const Frustum& f, std::vector<Batch>& out, std::vector<InstanceGpu>& data, bool markDrawn,
                               const glm::vec3* eye) {
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
    std::vector<std::pair<const Key*, std::vector<Instance*>*>> kept;
    for (auto& [key, list] : groups)
        if (list.size() >= 2) kept.push_back({ &key, &list });
    if (eye) {
        // Nearest first, inside each instanced draw and across draws.
        std::vector<glm::vec3> nearest(kept.size());
        for (size_t k = 0; k < kept.size(); ++k) {
            std::vector<Instance*>& list = *kept[k].second;
            std::vector<glm::vec3> pos(list.size());
            for (size_t i = 0; i < list.size(); ++i) pos[i] = glm::vec3(list[i]->transform[3]);
            std::vector<Instance*> sorted;
            sorted.reserve(list.size());
            for (uint32_t i : frontToBackOrder(pos, *eye)) sorted.push_back(list[i]);
            list.swap(sorted);
            nearest[k] = glm::vec3(list.front()->transform[3]);
        }
        std::vector<std::pair<const Key*, std::vector<Instance*>*>> byDistance;
        byDistance.reserve(kept.size());
        for (uint32_t k : frontToBackOrder(nearest, *eye)) byDistance.push_back(kept[k]);
        kept.swap(byDistance);
    }
    for (auto& [key, list] : kept) {
        Batch b{ key->model, key->tex, key->overlay, static_cast<uint32_t>(data.size()), static_cast<uint32_t>(list->size()) };
        for (Instance* inst : *list) {
            data.push_back({ inst->transform, glm::vec4(inst->tint, 1.0f) });
            if (markDrawn) inst->batchedFrame = m_viewPass;
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
    m_occlusion = nullptr; // the light sees what the camera's map hides
    if (ctx.tile == 0) ++m_frame; // renderShadow runs first each frame (see Application's frame loop), once per view and cascade
    const int pass = static_cast<int>(kMaxViews + std::min(ctx.tile, kShadowPasses - 1));
    m_shadowPipeline->bind(*ctx.list);
    // Cull against the light's own frustum: things off camera still cast
    // shadows into view, things outside the shadow map can't.
    const Frustum lightFrustum = Frustum::fromViewProj(ctx.lightViewProj);
    if (m_showMeshes) skinVisible(lightFrustum, ctx.frameIndex);
    // Instanced groups first; what they drew is skipped below.
    if (m_showMeshes) {
        buildBatches(lightFrustum, m_batches, m_instanceData, false);
        uploadInstances(pass, ctx.frameIndex, m_instanceData);
        if (!m_batches.empty()) {
            m_instancedShadowPipeline->bind(*ctx.list);
            ShadowPushConstants pc{ ctx.lightViewProj, glm::mat4(1.0f) };
            ctx.list->pushConstants(*m_instancedShadowPipeline, rhi::kVertex, 0, sizeof(pc), &pc);
            VkBuffer ib = m_instanceBuffers[pass][ctx.frameIndex]->handle();
            for (const Batch& b : m_batches) {
                const LoadedModel& lm = *m_models[b.model];
                VkDeviceSize off = sizeof(InstanceGpu) * b.first;
                ctx.list->bindVertexBuffer(1, rhi::VulkanCommandList::buffer(ib), off);
                for (const GpuMesh& gm : lm.meshes) {
                    gm.mesh->bind(*ctx.list);
                    gm.mesh->drawInstanced(*ctx.list, b.count);
                }
            }
            m_shadowPipeline->bind(*ctx.list);
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
            ShadowPushConstants pc{ ctx.lightViewProj, inst.deformedInModelSpace ? inst.transform : glm::mat4(1.0f) }; // else already world space
            ctx.list->pushConstants(*m_shadowPipeline, rhi::kVertex, 0, sizeof(pc), &pc);
            for (SkinnedBuffers& sb : inst.deformed) {
                if (!sb.indexCount) continue;
                VkBuffer vb = sb.vertices[ctx.frameIndex]->handle();
                VkDeviceSize off = 0;
                ctx.list->bindVertexBuffer(0, rhi::VulkanCommandList::buffer(vb), off);
                ctx.list->bindIndexBuffer(rhi::VulkanCommandList::buffer(sb.indices->handle()), 0, rhi::IndexType::U32);
                ctx.list->drawIndexed(sb.indexCount, 1, 0, 0, 0);
            }
            continue;
        }
        ShadowPushConstants pc{ ctx.lightViewProj, inst.transform };
        ctx.list->pushConstants(*m_shadowPipeline, rhi::kVertex, 0, sizeof(pc), &pc);
        const LoadedModel& lm = *m_models[inst.model];
        size_t si = 0;
        for (const GpuMesh& gm : lm.meshes) {
            if (gm.skinned) {
                SkinnedBuffers& sb = inst.skinned[si++];
                VkBuffer vb = sb.vertices[ctx.frameIndex]->handle();
                VkDeviceSize off = 0;
                ctx.list->bindVertexBuffer(0, rhi::VulkanCommandList::buffer(vb), off);
                ctx.list->bindIndexBuffer(rhi::VulkanCommandList::buffer(sb.indices->handle()), 0, rhi::IndexType::U32);
                ctx.list->drawIndexed(sb.indexCount, 1, 0, 0, 0);
            } else {
                gm.mesh->bind(*ctx.list);
                gm.mesh->draw(*ctx.list);
            }
        }
    }
}

void ModelModule::render(const RenderContext& ctx) {
    // Split screen: once per view; the stats add up the views.
    if (ctx.viewIndex == 0) {
        m_drawCalls = 0;
        m_culled = 0;
        m_occluded = 0;
        m_instancedCount = 0;
    }
    m_viewPass = m_frame * kMaxViews + ctx.viewIndex;
    const int pass = static_cast<int>(std::min(ctx.viewIndex, kMaxViews - 1));
    // Frustum culling (docs/OPTIMIZATION.md #23): an instance entirely outside
    // the view is neither skinned, uploaded nor drawn.
    const Frustum frustum = Frustum::fromViewProj(ctx.proj * ctx.view);
    // Occlusion culling (ENG-F13): the renderer's depth map, for the first view (null in split screen or when off).
    m_occlusion = ctx.viewIndex == 0 ? m_app->renderer().occlusionMap() : nullptr;
    VkDescriptorSet overlay = m_overlaySet ? m_overlaySet : ctx.defaultMaterialTextureDescriptorSet;
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, m_defaultSurface, overlay };
    const uint32_t pcStages = rhi::kVertex | rhi::kFragment;
    if (m_showMeshes) {
        // Instancing (docs/OPTIMIZATION.md #25): 2+ visible copies of the same
        // rigid model (same texture/overlay) are one draw per mesh part.
        buildBatches(frustum, m_batches, m_instanceData, true, &ctx.cameraPos);
        uploadInstances(pass, ctx.frameIndex, m_instanceData);
        if (!m_batches.empty()) {
            m_instancedPipeline->bind(*ctx.list);
            ctx.list->bindGroups(*m_instancedPipeline, 0, rhi::VulkanCommandList::groups(sets, 4));
            VkBuffer ib = m_instanceBuffers[pass][ctx.frameIndex]->handle();
            VkDescriptorSet bound = m_defaultSurface;
            for (const Batch& b : m_batches) {
                const LoadedModel& lm = *m_models[b.model];
                VkDeviceSize off = sizeof(InstanceGpu) * b.first;
                ctx.list->bindVertexBuffer(1, rhi::VulkanCommandList::buffer(ib), off);
                for (const GpuMesh& gm : lm.meshes) {
                    const GpuMaterial& mat = lm.materials[gm.material];
                    VkDescriptorSet tex = mat.textureSet ? mat.textureSet : m_defaultSurface;
                    if (b.textureOverride && mat.albedo) tex = b.textureOverride;
                    if (tex != bound) {
                        ctx.list->bindGroups(*m_instancedPipeline, 2, rhi::VulkanCommandList::groups(&tex, 1));
                        bound = tex;
                    }
                    PushConstants pc{ glm::mat4(1.0f), glm::vec4(mat.metallic, mat.roughness, b.overlay ? m_overlayTile : 0.0f, m_overlayStrength),
                                      glm::vec4(mat.color, 0.0f) };
                    ctx.list->pushConstants(*m_instancedPipeline, pcStages, 0, sizeof(pc), &pc);
                    gm.mesh->bind(*ctx.list);
                    gm.mesh->drawInstanced(*ctx.list, b.count);
                    ++m_drawCalls;
                }
                m_instancedCount += b.count;
            }
        }
        m_pipeline->bind(*ctx.list);
        ctx.list->bindGroups(*m_pipeline, 0, rhi::VulkanCommandList::groups(sets, 4));
        VkDescriptorSet boundTexture = m_defaultSurface;
        // Single draws nearest first too (early-Z, #37). Deformed parts are
        // in world space already (identity transform): they sort by their
        // own origin, i.e. last among close objects, which is harmless.
        skinVisible(frustum, ctx.frameIndex);
        m_singles.clear();
        std::vector<glm::vec3> singlePos;
        for (auto& [id, inst] : m_instances) {
            if (!inst.visible) continue;
            m_countOccluded = true; // each instance is counted once, here
            const bool visible = mightBeVisible(inst, frustum);
            m_countOccluded = false;
            if (!visible) { ++m_culled; continue; }
            if (inst.batchedFrame == m_viewPass) continue; // drawn instanced above
            m_singles.push_back(&inst);
            singlePos.push_back(glm::vec3(inst.transform[3]));
        }
        for (uint32_t idx : frontToBackOrder(singlePos, ctx.cameraPos)) {
            Instance& inst = *m_singles[idx];
            skinInstance(inst, ctx.frameIndex);
            uploadDeformed(inst, ctx.frameIndex);
            const LoadedModel& lm = *m_models[inst.model];
            const bool deformed = !inst.deformed.empty();
            const bool worldSpace = deformed && !inst.deformedInModelSpace;
            size_t si = 0;
            for (const GpuMesh& gm : lm.meshes) {
                const GpuMaterial& mat = lm.materials[gm.material];
                // A texture variant (e.g. Synty's _Texture_02) replaces the
                // atlas of every textured material; untextured ones keep
                // their plain color.
                VkDescriptorSet tex = mat.textureSet ? mat.textureSet : m_defaultSurface;
                if (inst.textureOverride && mat.albedo) tex = inst.textureOverride;
                if (tex != boundTexture) {
                    ctx.list->bindGroups(*m_pipeline, 2, rhi::VulkanCommandList::groups(&tex, 1));
                    boundTexture = tex;
                }
                float tile = inst.overlay ? m_overlayTile : 0.0f;
                // Deformed parts are already in world space (identity
                // model matrix) but their overlay positions are in model
                // space: overlayScale carries the object's scale for them.
                PushConstants pc{ worldSpace ? glm::mat4(1.0f) : inst.transform, glm::vec4(mat.metallic, mat.roughness, tile, m_overlayStrength),
                                  glm::vec4(mat.color * inst.tint, worldSpace ? instanceScale(inst.transform) : 0.0f) };
                ctx.list->pushConstants(*m_pipeline, pcStages, 0, sizeof(pc), &pc);
                if (deformed) {
                    SkinnedBuffers& sb = inst.deformed[gm.meshIndex];
                    if (sb.indexCount) {
                        VkBuffer vb = sb.vertices[ctx.frameIndex]->handle();
                        VkDeviceSize off = 0;
                        ctx.list->bindVertexBuffer(0, rhi::VulkanCommandList::buffer(vb), off);
                        ctx.list->bindIndexBuffer(rhi::VulkanCommandList::buffer(sb.indices->handle()), 0, rhi::IndexType::U32);
                        ctx.list->drawIndexed(sb.indexCount, 1, 0, 0, 0);
                    }
                } else if (gm.skinned) {
                    SkinnedBuffers& sb = inst.skinned[si++];
                    VkBuffer vb = sb.vertices[ctx.frameIndex]->handle();
                    VkDeviceSize off = 0;
                    ctx.list->bindVertexBuffer(0, rhi::VulkanCommandList::buffer(vb), off);
                    ctx.list->bindIndexBuffer(rhi::VulkanCommandList::buffer(sb.indices->handle()), 0, rhi::IndexType::U32);
                    ctx.list->drawIndexed(sb.indexCount, 1, 0, 0, 0);
                } else {
                    gm.mesh->bind(*ctx.list);
                    gm.mesh->draw(*ctx.list);
                }
                ++m_drawCalls;
            }
        }
    }

    m_occlusion = nullptr; // only this view's draws above use it
    if (!m_showBones) return;
    m_bonePipeline->bind(*ctx.list);
    sets[3] = ctx.defaultMaterialTextureDescriptorSet;
    ctx.list->bindGroups(*m_bonePipeline, 0, rhi::VulkanCommandList::groups(sets, 4));
    m_boneMesh->bind(*ctx.list);
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
            ctx.list->pushConstants(*m_bonePipeline, pcStages, 0, sizeof(pc), &pc);
            m_boneMesh->draw(*ctx.list);
        }
    }
}

void ModelModule::shutdown() {
    m_skinWorkers.reset();
    m_instances.clear();
    if (m_skinPipeline) vkDestroyPipeline(m_device, m_skinPipeline, nullptr);
    if (m_skinLayout) vkDestroyPipelineLayout(m_device, m_skinLayout, nullptr);
    if (m_skinSetLayout) vkDestroyDescriptorSetLayout(m_device, m_skinSetLayout, nullptr);
    m_skinPipeline = VK_NULL_HANDLE;
    m_skinLayout = VK_NULL_HANDLE;
    m_skinSetLayout = VK_NULL_HANDLE;
    m_models.clear();
    m_surfaceSets.clear();
    m_defaultSurface = VK_NULL_HANDLE;
    if (m_surfacePool) vkDestroyDescriptorPool(m_device, m_surfacePool, nullptr);
    if (m_surfaceLayout) vkDestroyDescriptorSetLayout(m_device, m_surfaceLayout, nullptr);
    m_surfacePool = VK_NULL_HANDLE;
    m_surfaceLayout = VK_NULL_HANDLE;
    m_flatNormal.reset();
}

namespace {

constexpr float kKkeTriangleShare = 0.1f; // KKE ray tracing traces about this share of a part's triangles

float linearChannel(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// A mesh part's colour as it mostly looks: the material's colour times its
// texture's dominant colour where the part's UVs fall (a Synty atlas holds
// many colours; this is the one this part uses most).
glm::vec4 partColor(const ModelData& data, const ModelMesh& mesh) {
    glm::vec3 c(1.0f);
    if (mesh.material < data.materials.size()) {
        const ModelMaterial& m = data.materials[mesh.material];
        c = m.baseColor;
        if (!m.albedoTexture.empty()) {
            std::vector<glm::vec2> uvs;
            uvs.reserve(mesh.vertices.size());
            for (const ModelVertex& v : mesh.vertices) uvs.push_back(v.uv);
            const InteriorFill fill = interiorFillFromTexture(m.albedoTexture, uvs);
            if (fill.valid) c *= glm::vec3(linearChannel(fill.dominant.r), linearChannel(fill.dominant.g), linearChannel(fill.dominant.b));
        }
    }
    return glm::vec4(c, 1.0f);
}

} // namespace

const ModelModule::StandIn& ModelModule::rigidFor(ModelId id, RayScene& scene, bool classic) {
    auto& cache = classic ? m_full : m_standIns;
    if (auto it = cache.find(id); it != cache.end()) return it->second;
    StandIn& out = cache[id];
    const auto model = m_models.find(id);
    if (model == m_models.end()) return out;
    const ModelData& data = model->second->data;
    for (const ModelMesh& m : data.meshes) {
        if (m.skinned || m.indices.size() < 3) continue;
        // Classic: every triangle. KKE: about a tenth of them, over the same vertices.
        const std::vector<uint32_t> indices = classic ? m.indices : simplifyIndices(m, kKkeTriangleShare);
        if (indices.size() < 3) continue;
        std::vector<glm::vec3> positions, normals;
        positions.reserve(m.vertices.size());
        normals.reserve(m.vertices.size());
        for (const ModelVertex& v : m.vertices) {
            positions.push_back(v.position);
            normals.push_back(v.normal);
        }
        out.parts.push_back({ scene.addTriangles(positions, indices, &normals), partColor(data, m) });
    }
    return out;
}

const Buffer* ModelModule::simplifiedIndices(ModelId id, uint32_t meshIndex, uint32_t& count) {
    auto& parts = m_simplified[id];
    const LoadedModel& lm = *m_models.at(id);
    if (parts.empty()) {
        parts.resize(lm.data.meshes.size());
        for (size_t i = 0; i < lm.data.meshes.size(); ++i) {
            const ModelMesh& m = lm.data.meshes[i];
            if (!m.skinned || m.indices.size() < 3) continue;
            const std::vector<uint32_t> idx = simplifyIndices(m, kKkeTriangleShare);
            parts[i].count = static_cast<uint32_t>(idx.size());
            parts[i].buffer = std::make_unique<Buffer>(Buffer::createDeviceLocal(
                m_app->device(), idx.data(), idx.size() * sizeof(uint32_t),
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
        }
    }
    count = meshIndex < parts.size() ? parts[meshIndex].count : 0;
    return meshIndex < parts.size() ? parts[meshIndex].buffer.get() : nullptr;
}

void ModelModule::addToRayScene(RayScene& scene, std::vector<RayMaterial>& materials, const RaySceneContext& context) {
    if (&scene != m_standInScene || context.classic != m_rayClassic) {
        // A new scene (its geometry was the old one's), or the other kind of ray tracing.
        if (&scene == m_standInScene)
            for (const auto& [iid, parts] : m_skinnedRays)
                for (const StandIn::Part& p : parts) scene.remove(p.geometry);
        m_standIns.clear();
        m_full.clear();
        m_skinnedRays.clear();
        m_standInScene = &scene;
        m_rayClassic = context.classic;
    }
    // Instances gone since last frame: their skinned geometry goes too.
    for (auto it = m_skinnedRays.begin(); it != m_skinnedRays.end();) {
        if (m_instances.count(it->first)) {
            ++it;
            continue;
        }
        for (const StandIn::Part& p : it->second) scene.remove(p.geometry);
        it = m_skinnedRays.erase(it);
    }
    for (auto& [iid, inst] : m_instances) {
        if (!inst.visible) continue;
        for (const StandIn::Part& part : rigidFor(inst.model, scene, context.classic).parts) {
            materials.push_back(scene.material(part.geometry, part.color * glm::vec4(inst.tint, 1.0f)));
            scene.add(part.geometry, inst.transform, static_cast<uint32_t>(materials.size() - 1));
        }
        // Skinned parts: their skinned vertices this frame (model space), refitted; KKE over a simplified index list.
        if (inst.skinned.empty()) continue;
        std::vector<StandIn::Part>& parts = m_skinnedRays[iid];
        const LoadedModel& lm = *m_models.at(inst.model);
        size_t si = 0;
        for (const GpuMesh& gm : lm.meshes) {
            if (!gm.skinned) continue;
            if (si >= inst.skinned.size()) break;
            SkinnedBuffers& sb = inst.skinned[si];
            const Buffer& vb = *sb.vertices[context.frame % Renderer::kMaxFramesInFlight];
            const uint32_t vertexCount = static_cast<uint32_t>(lm.data.meshes[gm.meshIndex].vertices.size());
            uint32_t indexCount = sb.indexCount;
            const Buffer* ib = sb.indices.get();
            if (!context.classic) ib = simplifiedIndices(inst.model, gm.meshIndex, indexCount);
            if (!ib || indexCount < 3) {
                ++si;
                continue;
            }
            if (parts.size() <= si)
                parts.push_back({ scene.addDynamic(vb.deviceAddress(), sizeof(Vertex), vertexCount, ib->deviceAddress(), indexCount),
                                  partColor(lm.data, lm.data.meshes[gm.meshIndex]) });
            scene.refit(context.cmd, parts[si].geometry, vb.deviceAddress());
            // The hit reads the skinned vertex (kke::Vertex: normal at byte 24; its colour slot isn't a colour here).
            RayMaterial m;
            m.color = parts[si].color * glm::vec4(inst.tint, 1.0f);
            m.vertices = vb.deviceAddress();
            m.indices = ib->deviceAddress();
            m.stride = sizeof(Vertex);
            m.normalOffset = offsetof(Vertex, normal);
            materials.push_back(m);
            scene.add(parts[si].geometry, inst.transform, static_cast<uint32_t>(materials.size() - 1));
            ++si;
        }
    }
}

} // namespace kke
