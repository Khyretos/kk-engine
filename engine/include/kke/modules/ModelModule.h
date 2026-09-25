#pragma once

#include "kke/Module.h"
#include "kke/Buffer.h"
#include "kke/Mesh.h"
#include "kke/ModelAsset.h"
#include "kke/Pipeline.h"
#include "kke/Renderer.h"
#include "kke/Texture.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace kke {

// Loads FBX/OBJ models (kke::loadModel — e.g. Synty packs) and draws any
// number of placed instances of them, lit, textured and shadow-casting,
// with the same PBR shader as the rest of the engine.
//
// Skinned models (characters) are skinned on the CPU every frame from
// each instance's pose — either an animation clip, a pose you edit bone
// by bone (setBoneLocal), or world-space bone transforms supplied by
// something else entirely (setBoneWorldOverride — how a ragdoll drives
// a character). CPU skinning keeps this simple and needs no new shader
// or vertex format; a Synty character is ~2k triangles, which costs a
// fraction of a millisecond. GPU skinning is the upgrade path for crowds
// (see ROADMAP.md).
//
// Usage:
//   auto& models = app.addModule<kke::ModelModule>();   // before init
//   ModelId tree = models.load("assets/synty/.../SM_Tree.fbx");
//   models.spawn(tree, glm::translate(glm::mat4(1), {3, 0, 0}));
class ModelModule : public Module {
public:
    using ModelId = uint32_t;     // 0 = invalid
    using InstanceId = uint32_t;  // 0 = invalid

    const char* name() const override { return "Models"; }
    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void render(const RenderContext& ctx) override;
    void renderShadow(const ShadowRenderContext& ctx) override;
    void shutdown() override;

    // Loads (or returns the cached) model. Only valid after init(). On
    // failure logs the reason and returns 0 — a missing prop shouldn't
    // take the whole game down.
    ModelId load(const std::string& path, const ModelLoadOptions& options = {});
    const ModelData* model(ModelId id) const;

    InstanceId spawn(ModelId model, const glm::mat4& transform = glm::mat4(1.0f));
    void remove(InstanceId instance);
    void setTransform(InstanceId instance, const glm::mat4& transform);
    glm::mat4 transform(InstanceId instance) const;
    void setVisible(InstanceId instance, bool visible);
    // Multiplies the material color (e.g. to tell identical dummies apart).
    void setTint(InstanceId instance, const glm::vec3& tint);

    // ---- skinned instances
    // Plays one of the model's clips (ModelData::animations); -1 stops and
    // returns to the rest pose.
    void playAnimation(InstanceId instance, int clip, bool loop = true, float speed = 1.0f);
    // Local (parent-relative) transform of every bone, starting at the rest
    // pose. Edit and the next frame is skinned from it. Ignored while an
    // animation is playing or a world override is set.
    std::vector<glm::mat4>* boneLocals(InstanceId instance);
    // Model-space transform of each bone, for effects or a ragdoll to read.
    std::vector<glm::mat4> boneWorld(InstanceId instance) const;
    // Drive the skeleton from outside (e.g. physics) with model-space bone
    // transforms; empty vector clears the override.
    void setBoneWorldOverride(InstanceId instance, std::vector<glm::mat4> world);

    // Draws every skinned instance's skeleton on top (depth test off).
    void setShowBones(bool show) { m_showBones = show; }
    bool showBones() const { return m_showBones; }
    void setShowMeshes(bool show) { m_showMeshes = show; }

    size_t instanceCount() const { return m_instances.size(); }
    size_t drawCallsLastFrame() const { return m_drawCalls; }

private:
    struct GpuMaterial { glm::vec3 color; float metallic, roughness; VkDescriptorSet textureSet = VK_NULL_HANDLE; };
    struct GpuMesh { std::unique_ptr<Mesh> mesh; uint32_t material = 0; bool skinned = false; uint32_t meshIndex = 0; };
    struct LoadedModel {
        ModelData data;
        std::vector<GpuMesh> meshes;
        std::vector<GpuMaterial> materials;
    };
    struct SkinnedBuffers {
        std::unique_ptr<Buffer> vertices[Renderer::kMaxFramesInFlight];
        std::unique_ptr<Buffer> indices;
        std::vector<Vertex> cpu;
        uint32_t indexCount = 0;
    };
    struct Instance {
        ModelId model = 0;
        glm::mat4 transform{1.0f};
        glm::vec3 tint{1.0f};
        bool visible = true;
        // skinned only
        std::vector<glm::mat4> locals;
        std::vector<glm::mat4> worldOverride;
        std::vector<SkinnedBuffers> skinned; // one per skinned mesh of the model
        int clip = -1;
        float clipTime = 0.0f, clipSpeed = 1.0f;
        bool clipLoop = true;
        uint64_t skinnedFrame = ~0ull;
    };

    VkDescriptorSet textureSetFor(const std::string& path);
    void skinInstance(Instance& inst, uint32_t frameIndex);
    std::vector<glm::mat4> currentBoneWorld(const Instance& inst) const;

    Application* m_app = nullptr;
    std::unique_ptr<Pipeline> m_pipeline, m_shadowPipeline, m_bonePipeline;
    std::unique_ptr<Mesh> m_boneMesh;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_textures;
    std::unordered_map<std::string, VkDescriptorSet> m_textureSets;

    std::unordered_map<std::string, ModelId> m_modelByPath;
    std::unordered_map<ModelId, std::unique_ptr<LoadedModel>> m_models;
    std::unordered_map<InstanceId, Instance> m_instances;
    ModelId m_nextModel = 1;
    InstanceId m_nextInstance = 1;
    bool m_showBones = false, m_showMeshes = true;
    uint64_t m_frame = 0;
    size_t m_drawCalls = 0;
};

} // namespace kke
