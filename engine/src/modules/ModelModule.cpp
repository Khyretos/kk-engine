#include "kke/modules/ModelModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/VulkanCheck.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace kke {

namespace {
// Matches model.vert/model.frag: material = (metallic, roughness, overlay
// tile size in metres or 0, overlay strength); tint.rgb multiplies color.
struct PushConstants { glm::mat4 model; glm::vec4 material; glm::vec4 tint; };
struct ShadowPushConstants { glm::mat4 lightViewProj; glm::mat4 model; };

Vertex toVertex(const ModelVertex& v, const glm::vec3& color) {
    return Vertex{ v.position, color, v.normal, v.uv };
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

    auto loaded = std::make_unique<LoadedModel>();
    try {
        loaded->data = loadModel(path, options);
    } catch (const std::exception& e) {
        log::get(name())->error("{}", e.what());
        return 0;
    }
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
            for (const ModelVertex& v : m.vertices) verts.push_back(toVertex(v, data.materials[m.material].baseColor));
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
                sb.cpu[v] = toVertex(src.vertices[v], lm.data.materials[src.material].baseColor);
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
    for (size_t m = 0; m < lm.data.meshes.size(); ++m) {
        const std::vector<ModelVertex>& src = m < parts.size() ? parts[m] : std::vector<ModelVertex>{};
        SkinnedBuffers sb;
        const glm::vec3 color = lm.data.materials[lm.data.meshes[m].material].baseColor;
        sb.cpu.reserve(src.size());
        for (const ModelVertex& v : src) sb.cpu.push_back(toVertex(v, color));
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
        const glm::vec3 color = lm.data.materials[src.material].baseColor; // tint: push constant
        for (size_t v = 0; v < src.vertices.size(); ++v) {
            glm::vec3 p, n;
            skinVertex(src.vertices[v], skin, p, n);
            sb.cpu[v] = Vertex{ p, color, n, src.vertices[v].uv };
        }
        sb.vertices[frameIndex]->upload(sb.cpu.data(), sb.cpu.size() * sizeof(Vertex));
    }
}

void ModelModule::renderShadow(const ShadowRenderContext& ctx) {
    ++m_frame; // renderShadow runs first each frame (see Application's frame loop)
    m_shadowPipeline->bind(ctx.cmd);
    for (auto& [id, inst] : m_instances) {
        if (!inst.visible || !m_showMeshes) continue;
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
    VkDescriptorSet overlay = m_overlaySet ? m_overlaySet : ctx.defaultMaterialTextureDescriptorSet;
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, ctx.defaultMaterialTextureDescriptorSet, overlay };
    const VkShaderStageFlags pcStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    if (m_showMeshes) {
        m_pipeline->bind(ctx.cmd);
        vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(), 0, 4, sets, 0, nullptr);
        VkDescriptorSet boundTexture = ctx.defaultMaterialTextureDescriptorSet;
        for (auto& [id, inst] : m_instances) {
            if (!inst.visible) continue;
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
                PushConstants pc{ deformed ? glm::mat4(1.0f) : inst.transform, glm::vec4(mat.metallic, mat.roughness, tile, m_overlayStrength),
                                  glm::vec4(inst.tint, 1.0f) };
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
            PushConstants pc{ m, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), glm::vec4(1.0f) };
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
