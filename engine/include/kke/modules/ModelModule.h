#pragma once

#include "kke/AnimEvents.h"
#include "kke/Capabilities.h"
#include "kke/Module.h"
#include "kke/Buffer.h"
#include "kke/rhi/Device.h"
#include "kke/JigglePhysics.h"
#include "kke/Mesh.h"
#include "kke/ModelAsset.h"
#include "kke/Pipeline.h"
#include "kke/Renderer.h"
#include "kke/ShadowMap.h"
#include "kke/Texture.h"

#include <glm/glm.hpp>

#include <functional>
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
class SkinWorkers;

class ModelModule : public Module, public IRayContributor {
public:
    using ModelId = uint32_t;     // 0 = invalid
    using InstanceId = uint32_t;  // 0 = invalid
    ModelModule();
    ~ModelModule() override;

    const char* name() const override { return "Models"; }
    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void render(const RenderContext& ctx) override;
    void renderShadow(const ShadowRenderContext& ctx) override;
    // ENG-G08: skins every visible skinned instance on the GPU (shaders/skin.comp).
    void computeEveryFrame(VkCommandBuffer cmd) override;
    void shutdown() override;

    // Loads (or returns the cached) model. Only valid after init(). On
    // failure logs the reason and returns 0 — a missing prop shouldn't
    // take the whole game down.
    ModelId load(const std::string& path, const ModelLoadOptions& options = {});
    // Registers model data made or edited in code (e.g. a character given
    // soft-tissue bones by kke::addJiggleBone) under `key`, as if loaded
    // from a file of that name. Same key again returns the first one.
    // transient: a short-lived model (thumbnails load thousands): no
    // "loaded" log line, and its buffers are written directly instead of
    // uploaded with a wait for the GPU (see Mesh::Memory).
    ModelId add(ModelData data, const std::string& key, bool transient = false);
    const ModelData* model(ModelId id) const;
    // Frees a model nothing uses any more (its GPU buffers once the frames
    // in flight are done with them). False, and nothing freed, while an
    // instance still draws it.
    bool unload(ModelId model);
    // Draws a model once, outside the world, into whatever pass is open
    // (one compatible with the main pass): for thumbnails and previews.
    // Skinned parts draw in their bind pose. The three sets are the lit
    // pipeline's sets 0-2 (camera and lights, shadow map, default texture).
    void drawStandalone(VkCommandBuffer cmd, ModelId model, const glm::mat4& transform, VkDescriptorSet lighting, VkDescriptorSet shadowMap,
                        VkDescriptorSet defaultTexture);

    InstanceId spawn(ModelId model, const glm::mat4& transform = glm::mat4(1.0f));
    void remove(InstanceId instance);
    void setTransform(InstanceId instance, const glm::mat4& transform);
    glm::mat4 transform(InstanceId instance) const;
    // The model an instance draws (0 for an unknown instance).
    ModelId instanceModel(InstanceId instance) const;
    void setVisible(InstanceId instance, bool visible);
    // Multiplies the material color (e.g. to tell identical dummies apart).
    void setTint(InstanceId instance, const glm::vec3& tint);
    // Draw this instance with another image in place of its materials'
    // albedo textures — Synty packs ship recoloured variants of one atlas
    // (PolygonPrototype_Texture_01..10) that share the same UVs. Only
    // materials that had a texture are affected. "" = back to the model's own.
    void setTextureOverride(InstanceId instance, const std::string& texturePath);
    std::string textureOverride(InstanceId instance) const;
    // A world-space triplanar overlay multiplied onto every instance (the
    // look of Synty's Prototype grid shader, using its *_Grid_* images):
    // one image tile covers `tileSizeMetres`. "" turns it off.
    void setWorldOverlay(const std::string& texturePath, float tileSizeMetres = 2.0f, float strength = 1.0f);
    void setOverlayEnabled(InstanceId instance, bool enabled);

    // ---- skinned instances
    // Plays one of the model's clips (ModelData::animations); -1 stops and
    // returns to the rest pose.
    void playAnimation(InstanceId instance, int clip, bool loop = true, float speed = 1.0f);
    // Animation events (kke/AnimEvents.h) on a model's clip: `name` at
    // `seconds` into clip `clip`, for every instance that plays it.
    // clearClipEvents(model, -1) removes all of the model's.
    void addClipEvent(ModelId model, int clip, float seconds, const std::string& name);
    void clearClipEvents(ModelId model, int clip = -1);
    // What playing clips passed during the last update(), each
    // instance's in time order (once per pass: a looping clip every cycle;
    // events at 0 on the first update after playAnimation()).
    struct ClipEventHit { InstanceId instance; int clip; std::string name; };
    const std::vector<ClipEventHit>& clipEvents() const { return m_clipEvents; }
    // Local (parent-relative) transform of every bone, starting at the rest
    // pose. Edit and the next frame is skinned from it. Ignored while an
    // animation is playing or a world override is set.
    std::vector<glm::mat4>* boneLocals(InstanceId instance);
    // Model-space transform of each bone, for effects or a ragdoll to read.
    std::vector<glm::mat4> boneWorld(InstanceId instance) const;
    // Drive the skeleton from outside (e.g. physics) with model-space bone
    // transforms; empty vector clears the override. The clip keeps
    // advancing boneLocals() underneath, so a blend from the override back
    // to the animation (a ragdoll standing up) sees it play.
    void setBoneWorldOverride(InstanceId instance, std::vector<glm::mat4> world);
    // A procedural layer on top of the clip (kke/ProceduralAnim.h: look-at,
    // foot placement, ...): called in update() with the pose the clip (or
    // boneLocals) gives, as model-space bone transforms, and the frame's dt;
    // what it leaves there is what's skinned and what boneWorld() returns.
    // Not called while a world override is set. nullptr removes it.
    using PoseModifier = std::function<void(std::vector<glm::mat4>& modelSpaceBones, float dt)>;
    void setPoseModifier(InstanceId instance, PoseModifier modifier);
    // Jiggle on the skin itself (kke::JiggleSkin::offsets()): displaces
    // skinned vertices near each zone after skinning. Empty clears.
    void setSkinJiggle(InstanceId instance, std::vector<SkinJiggleOffset> zones);

    // Draw a (non-skinned) instance from caller-supplied world-space
    // vertices instead of mesh + transform: one position/normal vector per
    // mesh part, in ModelData::meshes[i].vertices order. This is how a
    // physics-simulated prop is drawn (PhysicsModule::deformEmbedded moves
    // the vertices; UVs, materials and textures stay the model's own).
    // Uploaded once per frame in flight after each call; empty clears.
    // modelSpace: the vertices are in the model's own space instead, and
    // the instance's transform still places it (a dented car body that
    // keeps driving: upload only when a dent changes it, not every frame).
    void setDeformedVertices(InstanceId instance, const std::vector<std::vector<glm::vec3>>& positions,
                             const std::vector<std::vector<glm::vec3>>& normals, bool modelSpace = false);
    bool isDeformed(InstanceId instance) const;
    // Replace the geometry a deformed instance draws with, per mesh part
    // (e.g. a finer, unshared triangle soup for a breakable prop — see
    // kke::TriangleSoup). Materials stay the part's own; UVs come from
    // `vertices`. Afterwards setDeformedVertices() takes one position per
    // vertex given here. Clearing the deformation clears this too.
    void setDeformedTopology(InstanceId instance, const std::vector<std::vector<ModelVertex>>& vertices);

    // Draws every skinned instance's skeleton on top (depth test off).
    void setShowBones(bool show) { m_showBones = show; }
    bool showBones() const { return m_showBones; }
    void setShowMeshes(bool show) { m_showMeshes = show; }

    size_t instanceCount() const { return m_instances.size(); }
    size_t drawCallsLastFrame() const { return m_drawCalls; }
    // Instances skipped last frame because they were outside the camera
    // frustum or hidden behind others (see render()), and of those the
    // hidden ones (occlusion culling, ENG-F13, kke/Occlusion.h).
    size_t culledLastFrame() const { return m_culled; }
    size_t occludedLastFrame() const { return m_occluded; }
    // Instances drawn through instancing last frame (N copies of one
    // model = one draw per mesh part), and toggling it (A/B comparisons).
    size_t instancedLastFrame() const { return m_instancedCount; }
    // Ray tracing (ENG-F02): every visible instance, in the colour each part
    // mostly shows (its texture's dominant colour as the mesh uses it, times
    // the material's), its skinned parts refitted each frame from their
    // skinned vertices. Classic: every triangle. KKE: stand-ins rays can
    // afford, about a tenth of the triangles (meshoptimizer) over the same
    // vertices, so a character's reflection still moves as it does.
    void addToRayScene(RayScene& scene, std::vector<RayMaterial>& materials, const RaySceneContext& context) override;

    void setInstancingEnabled(bool on) { m_instancing = on; }
    bool instancingEnabled() const { return m_instancing; }

private:
    // textureSet = this material's set 2 (surfaceSetFor); albedo = it has an
    // albedo texture (a texture override replaces only those).
    struct GpuMaterial { glm::vec3 color; float metallic, roughness; VkDescriptorSet textureSet = VK_NULL_HANDLE; bool albedo = false; };
    // skinBind: a skinned part's bind vertices for shaders/skin.comp
    // (packSkinBind), uploaded once; null on the CPU skinning path.
    struct GpuMesh {
        std::unique_ptr<Mesh> mesh;
        uint32_t material = 0;
        bool skinned = false;
        uint32_t meshIndex = 0;
        std::unique_ptr<Buffer> skinBind;
    };
    struct LoadedModel {
        ModelData data;
        std::vector<GpuMesh> meshes;
        std::vector<GpuMaterial> materials;
        std::vector<std::vector<AnimEvent>> clipEvents; // per clip (addClipEvent), seconds
    };
    struct SkinnedBuffers {
        std::unique_ptr<rhi::Buffer> vertices[Renderer::kMaxFramesInFlight];
        std::unique_ptr<rhi::Buffer> indices;
        std::vector<Vertex> cpu;
        uint32_t indexCount = 0;
        VkDescriptorSet skinSets[Renderer::kMaxFramesInFlight] = {}; // GPU skinning: skin.comp's set per frame slot
    };
    // GPU skinning (ENG-G08): an instance's skin.comp descriptor sets come
    // from its own pool; its skin matrices and jiggle zones (packSkinData)
    // go up each frame into that frame slot's data buffer, which grows when
    // they no longer fit. Retired with the instance (frames in flight).
    struct GpuSkinState {
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> data[Renderer::kMaxFramesInFlight];
        size_t capacity[Renderer::kMaxFramesInFlight] = {}; // vec4s each data buffer holds
        std::vector<glm::vec4> packed;                        // this frame's packSkinData
        GpuSkinState() = default;
        GpuSkinState(const GpuSkinState&) = delete;
        GpuSkinState& operator=(const GpuSkinState&) = delete;
        ~GpuSkinState();
    };
    struct Instance {
        ModelId model = 0;
        glm::mat4 transform{1.0f};
        glm::vec3 tint{1.0f};
        bool visible = true;
        bool overlay = true;
        VkDescriptorSet textureOverride = VK_NULL_HANDLE;
        std::string textureOverridePath;
        // skinned only
        std::vector<glm::mat4> locals;
        std::vector<glm::mat4> worldOverride;
        PoseModifier modifier;
        std::vector<glm::mat4> modified;   // the modifier's result this frame
        std::vector<SkinJiggleOffset> skinJiggle;
        std::vector<SkinnedBuffers> skinned; // one per skinned mesh of the model
        std::vector<SkinnedBuffers> deformed; // one per mesh part while setDeformedVertices is active
        bool deformedInModelSpace = false;    // setDeformedVertices(..., modelSpace): drawn with `transform`
        uint64_t deformVersion = 0;
        uint64_t deformUploaded[Renderer::kMaxFramesInFlight] = {};
        int clip = -1;
        float clipTime = 0.0f, clipSpeed = 1.0f;
        bool clipLoop = true;
        bool clipStarted = false; // playAnimation() since the last update: events at 0 fire
        uint64_t skinnedFrame = ~0ull;
        uint64_t batchedFrame = ~0ull; // drawn instanced in this view this frame (see render())
        mutable uint64_t occlusionPass = ~0ull; // the draw `occluded` was worked out for (ENG-F13)
        mutable bool occluded = false;
        std::shared_ptr<GpuSkinState> gpuSkin; // GPU skinning only (createGpuSkin)
    };
    // Per-instance data for the instanced pipelines (binding 1).
    struct InstanceGpu { glm::mat4 model; glm::vec4 tint; };
    // A run of instances sharing model + texture + overlay: one draw per
    // mesh part. Built each frame after culling.
    struct Batch { ModelId model; VkDescriptorSet textureOverride; bool overlay; uint32_t first, count; };
    // eye set (camera passes): instances in each batch and the batches
    // themselves come out nearest first (kke/DrawOrder.h).
    void buildBatches(const struct Frustum& f, std::vector<Batch>& out, std::vector<InstanceGpu>& data, bool markDrawn,
                      const glm::vec3* eye = nullptr);
    void uploadInstances(int pass, uint32_t frameIndex, const std::vector<InstanceGpu>& data);
    std::vector<Instance*> m_singles; // render(): this view's non-instanced draws, reused

    VkDescriptorSet textureSetFor(const std::string& path);
    // ENG-G03: model.frag's set 2 = albedo, normal, roughness and metallic
    // maps (an empty path = that map's 1x1 default). One set per distinct
    // combination, shared by every material that uses it.
    VkDescriptorSet surfaceSetFor(const std::string& albedo, const std::string& normal, const std::string& roughness,
                                  const std::string& metallic);
    void skinInstance(Instance& inst, uint32_t frameIndex);
    // Skins every visible skinned instance not yet skinned this frame, on
    // the worker threads (a crowd of characters is the CPU's biggest job
    // here until skinning moves to the GPU); the draw loops then find them
    // done.
    void skinVisible(const struct Frustum& f, uint32_t frameIndex);
    // ENG-G08 GPU skinning (KKE_CPU_SKINNING=1 keeps the CPU path above).
    // createSkinPipeline: skin.comp's set layout, pipeline layout and pipeline.
    // createGpuSkin: an instance's pool and sets (at spawn).
    // writeSkinSets: points one frame slot's sets at the buffers.
    // packSkin: the bone matrices and zones into the slot's data buffer (any thread).
    // recordSkin: one dispatch per skinned part.
    void createSkinPipeline();
    void createGpuSkin(Instance& inst);
    void writeSkinSets(Instance& inst, uint32_t slot);
    void packSkin(Instance& inst, uint32_t slot);
    void recordSkin(VkCommandBuffer cmd, Instance& inst, uint32_t slot);
    void uploadDeformed(Instance& inst, uint32_t frameIndex);
    std::vector<glm::mat4> currentBoneWorld(const Instance& inst) const;
    struct StandIn {
        struct Part {
            uint32_t geometry = 0;  // RayScene::Geometry
            glm::vec4 color{1.0f};  // linear albedo
        };
        std::vector<Part> parts;
    };
    // Each model's rigid parts for rays: KKE about a tenth of the triangles, classic all of them.
    const StandIn& rigidFor(ModelId id, RayScene& scene, bool classic);
    std::unordered_map<ModelId, StandIn> m_standIns, m_full;
    // KKE: each model's skinned parts as simplified index lists over their skinned vertices.
    struct SimplifiedPart { std::unique_ptr<Buffer> buffer; uint32_t count = 0; };
    std::unordered_map<ModelId, std::vector<SimplifiedPart>> m_simplified;
    const Buffer* simplifiedIndices(ModelId id, uint32_t meshIndex, uint32_t& count);
    // Each instance's skinned parts, refitted every frame.
    std::unordered_map<InstanceId, std::vector<StandIn::Part>> m_skinnedRays;
    bool m_rayClassic = false;
    RayScene* m_standInScene = nullptr; // the scene m_standIns' geometry lives in
    void advanceClips(const UpdateContext& ctx);
    std::vector<ClipEventHit> m_clipEvents;
    std::vector<const AnimEvent*> m_passedEvents;
    // False only when the instance certainly can't be seen through `f`.
    bool mightBeVisible(const Instance& inst, const struct Frustum& f) const;

    Application* m_app = nullptr;
    std::unique_ptr<Pipeline> m_pipeline, m_shadowPipeline, m_bonePipeline;
    std::unique_ptr<Mesh> m_boneMesh;
    VkDevice m_device = VK_NULL_HANDLE;

    std::unordered_map<std::string, ModelId> m_modelByPath;
    std::unordered_map<ModelId, std::unique_ptr<LoadedModel>> m_models;
    std::unordered_map<InstanceId, Instance> m_instances;
    ModelId m_nextModel = 1;
    InstanceId m_nextInstance = 1;
    bool m_showBones = false, m_showMeshes = true;
    VkDescriptorSet m_overlaySet = VK_NULL_HANDLE;
    std::unique_ptr<rhi::BindGroupLayout> m_surfaceLayout; // set 2: albedo, normal, roughness, metallic (made by the RHI device)
    std::unique_ptr<Texture> m_flatNormal; // (128, 128, 255, 0): alpha 0 tells model.frag "no normal map"
    std::unordered_map<std::string, VkDescriptorSet> m_surfaceSets;
    VkDescriptorSet m_defaultSurface = VK_NULL_HANDLE;
    float m_overlayTile = 0.0f, m_overlayStrength = 1.0f;
    uint64_t m_frame = 0;
    size_t m_drawCalls = 0;
    size_t m_culled = 0;
    mutable size_t m_occluded = 0;
    bool m_countOccluded = false;
    const struct DepthMap* m_occlusion = nullptr; // set while render() draws the first view (ENG-F13)
    size_t m_instancedCount = 0;
    bool m_instancing = true;
    std::unique_ptr<Pipeline> m_instancedPipeline, m_instancedShadowPipeline;
    // [pass: 0 shadow, 1 main][frame in flight]
    // Instance lists: [0] the shadow pass, [1 + view] each split-screen
    // view (each culls against its own camera).
    // One instance buffer per pass and frame in flight: the views' passes
    // (0 .. kMaxViews-1), then their shadow passes (kMaxViews + ShadowRenderContext::tile).
    static constexpr uint32_t kShadowPasses = kMaxViews * ShadowMap::kCascades + ShadowMap::kMaxPointShadows * ShadowMap::kPointFaces;
    std::unique_ptr<rhi::Buffer> m_instanceBuffers[kMaxViews + kShadowPasses][Renderer::kMaxFramesInFlight];
    size_t m_instanceCapacity[kMaxViews + kShadowPasses][Renderer::kMaxFramesInFlight] = {};
    uint64_t m_viewPass = 0; // m_frame and the view: which draw batchedFrame means
    std::vector<Batch> m_batches;
    std::vector<InstanceGpu> m_instanceData;
    std::unique_ptr<SkinWorkers> m_skinWorkers;
    std::vector<Instance*> m_toSkin;
    bool m_gpuSkinning = false; // ENG-G08; KKE_CPU_SKINNING=1 turns it off
    VkDescriptorSetLayout m_skinSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_skinLayout = VK_NULL_HANDLE;
    VkPipeline m_skinPipeline = VK_NULL_HANDLE;
};

} // namespace kke
