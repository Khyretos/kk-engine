#include "kke/modules/PhysicsModule.h"

#if KKE_ENABLE_FEMFX

#include "kke/Log.h"
#include "kke/Application.h"

#include <imgui.h>
#include <AMD_FEMFX.h>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

// FEMFX declares these extern (FEMFXCommon.h) and expects the
// application to define them — an allocator hook, the same pattern as
// the task system callbacks below. Deliberately at GLOBAL scope, not
// inside namespace AMD or kke — matching exactly how FEMFX's own header
// declares them (verified directly: qualifying these as AMD:: produced
// a real compile error, "should have been declared inside 'AMD'",
// which is what confirmed the declaration is global, not guessed).
void* FmAlignedMalloc(size_t size, size_t alignment) {
    // std::aligned_alloc requires size to be a multiple of alignment —
    // a real C++17 requirement, not FEMFX's own. FEMFX always passes a
    // type's natural alignment as the second argument, so rounding size
    // up is correct and harmless regardless of what's being allocated.
    size_t roundedSize = ((size + alignment - 1) / alignment) * alignment;
    return std::aligned_alloc(alignment, roundedSize);
}

void FmAlignedFree(void* ptr) {
    std::free(ptr);
}

namespace kke {

namespace {

// FEMFX hardcodes FM_ASYNC_THREADING=1 — even a single-worker-thread
// scene requires a real implementation of this callback interface,
// there is no built-in synchronous fallback. What follows is a
// genuinely synchronous adapter, verified standalone before being
// wired in here: "submit a task" means "call it immediately, on the
// calling thread." This is honestly not real multithreading yet — see
// PhysicsModule.h's class comment and the README Roadmap.
struct SyncEvent {
    bool triggered = false;
};

int GetTaskSystemNumThreads() { return 1; }
int GetTaskSystemWorkerIndex() { return 0; }

void SubmitAsyncTask(const char* /*name*/, AMD::FmTaskFuncCallback func, void* data,
                      int32_t begin, int32_t end) {
    func(data, begin, end);
}

AMD::FmSyncEvent* CreateSyncEvent() {
    return reinterpret_cast<AMD::FmSyncEvent*>(new SyncEvent());
}

void DestroySyncEvent(AMD::FmSyncEvent* event) {
    delete reinterpret_cast<SyncEvent*>(event);
}

void WaitForSyncEvent(AMD::FmSyncEvent* event) {
    // Since SubmitAsyncTask above runs everything synchronously to
    // completion before returning, whatever would trigger this event
    // has already happened by the time anyone calls Wait.
    (void)event;
}

void TriggerSyncEvent(AMD::FmSyncEvent* event) {
    reinterpret_cast<SyncEvent*>(event)->triggered = true;
}

// A small fixed palette so multiple spawned objects are visually
// distinguishable from each other, cycled by handle — not tied to
// material in any way yet (that would need real per-material color
// mapping, future work), just enough to tell objects apart on screen.
const glm::vec3 kColorPalette[] = {
    {0.45f, 0.30f, 0.15f}, // wood
    {0.55f, 0.55f, 0.60f}, // stone-ish grey
    {0.70f, 0.15f, 0.10f}, // brick-ish red
    {0.20f, 0.45f, 0.25f}, // mossy green
};

} // namespace

// The actual scale mismatch, found empirically rather than assumed: the
// physics scene uses real units (gravity=9.88 m/s^2, an object falling
// from 5 units up, landing on a 100-unit-wide floor) — all correct and
// necessary for the simulation to behave physically. But this demo's
// existing camera orbits at a distance of 3.5 units around a *unit*
// cube (see OrbitCameraModule). Rendering the physics objects at their
// true scale meant the 100-unit floor alone filled nearly the entire
// view from that camera — not a rendering bug, just two very different
// scales sharing one camera. This factor shrinks only the *visual*
// representation; the simulation itself is untouched and still uses
// real units throughout.
constexpr float kRenderScale = 0.02f;

void PhysicsModule::init(Application& app) {
    m_app = &app;

    // Sized for kMaxObjects (see PhysicsModule.h) plus the one ground
    // rigid body — generous-but-modest numbers, not derived from first
    // principles, verified empirically by actually spawning up to the
    // cap via renderUi()'s button rather than assumed correct.
    AMD::FmSceneSetupParams sceneParams;
    sceneParams.maxTetMeshBuffers = kMaxObjects;
    sceneParams.maxTetMeshes = kMaxObjects;
    sceneParams.maxRigidBodies = 1;
    sceneParams.maxDistanceContacts = 64;
    sceneParams.maxVolumeContacts = 64;
    sceneParams.maxVolumeContactVerts = 64;
    sceneParams.maxDeformationConstraints = 64;
    sceneParams.maxGlueConstraints = 0;
    sceneParams.maxPlaneConstraints = 0;
    sceneParams.maxRigidBodyAngleConstraints = 0;
    sceneParams.maxBroadPhasePairs = 64;
    sceneParams.maxRigidBodyBroadPhasePairs = 64;
    sceneParams.maxSceneVerts = kMaxObjects * 4 + 16;
    sceneParams.maxTetMeshBufferFeatures = 128;
    sceneParams.maxConstraintSolverDataSize = 1 << 22;
    sceneParams.numWorkerThreads = 1;
    sceneParams.rigidBodiesExternal = false;

    m_scene = AMD::FmCreateScene(sceneParams);
    if (!m_scene) {
        throw std::runtime_error("FmCreateScene returned null");
    }

    AMD::FmTaskSystemCallbacks callbacks;
    callbacks.SetCallbacks(
        GetTaskSystemNumThreads,
        GetTaskSystemWorkerIndex,
        SubmitAsyncTask,
        CreateSyncEvent,
        DestroySyncEvent,
        WaitForSyncEvent,
        TriggerSyncEvent
    );
    AMD::FmSetSceneTaskSystemCallbacks(m_scene, callbacks);

    // --- A wide, flat, kinematic (static) ground plane, top surface at
    // y=0.
    AMD::FmRigidBodySetupParams groundParams;
    groundParams.halfDimX = 50.0f;
    groundParams.halfDimY = 0.5f;
    groundParams.halfDimZ = 50.0f;
    groundParams.mass = 1.0f; // irrelevant for a kinematic body, but must be nonzero for the inertia calc below
    groundParams.isKinematic = true;
    groundParams.collisionGroup = 0;
    groundParams.state.pos = AMD::FmInitVector3(0.0f, -0.5f, 0.0f);
    groundParams.bodyInertiaTensor = AMD::FmComputeBodyInertiaTensorForBox(50.0f, 0.5f, 50.0f, 1.0f);

    m_ground = AMD::FmCreateRigidBody(groundParams);
    if (!m_ground) {
        throw std::runtime_error("FmCreateRigidBody returned null");
    }
    AMD::FmAddRigidBodyToScene(m_scene, m_ground);

    // --- Shared render resources (see PhysicsModule.h for why these
    // are shared across every spawned object, not per-object). Reuses
    // the existing cube shaders directly — they just transform a
    // position by an MVP matrix and output a flat color, which is
    // exactly what a physics-driven mesh needs too, no new shaders
    // required.
    struct PhysicsPushConstants { glm::mat4 mvp; };
    {
        PipelineConfig config;
        config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PhysicsPushConstants) };
        m_pipeline = std::make_unique<Pipeline>(
            app.device(), app.renderer().renderPass(),
            "shaders/cube.vert.spv", "shaders/cube.frag.spv", config);
    }

    m_groundMesh = std::make_unique<Mesh>(Mesh::createCube(app.device()));

    // Tet index buffer: static, shared by every spawned object — a
    // tetrahedron's face topology never changes, and every object is
    // the same shape (see PhysicsModule.h "honest current limits").
    // Winding follows FEMFXTetMeshConnectivity.h's own documented
    // convention ("Faces CCW from exterior: 312, 203, 130, 021").
    const uint32_t tetIndices[12] = {
        3, 1, 2,  // face opposite vertex 0
        2, 0, 3,  // face opposite vertex 1
        1, 3, 0,  // face opposite vertex 2
        0, 2, 1,  // face opposite vertex 3
    };
    m_tetIndexBuffer = std::make_unique<Buffer>(Buffer::createDeviceLocal(
        app.device(), tetIndices, sizeof(tetIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));

    // The API dogfoods itself: the demo's one starting object is
    // created by calling the same public spawnTetrahedron() anyone
    // else (including renderUi()'s own button) would call, not
    // through separate one-off setup code.
    Material woodMaterial;
    woodMaterial.density = 700.0f;
    woodMaterial.stiffness = 1.0e7f;
    woodMaterial.poissonsRatio = 0.3f;
    woodMaterial.plasticYieldThreshold = 0.0f;
    woodMaterial.fractureStressThreshold = 1.0e8f; // high — this one isn't meant to fracture
    spawnTetrahedron(glm::vec3(0.0f, 5.0f, 0.0f), woodMaterial);

    log::get(name())->info("FEMFX scene created (ground + {} object(s), cap={})", m_objects.size(), kMaxObjects);
}

PhysicsModule::ObjectHandle PhysicsModule::spawnTetrahedron(const glm::vec3& position, const Material& material) {
    if (m_objects.size() >= kMaxObjects) {
        log::get(name())->warn("spawnTetrahedron: at cap ({}), ignoring", kMaxObjects);
        return kInvalidHandle;
    }

    auto obj = std::make_unique<SpawnedTet>();

    const uint numVerts = 4;
    const uint numTets = 1;

    // Same relative shape as always verified — 4 verts forming one
    // tetrahedron — just offset by `position` instead of a fixed spot.
    obj->restPositions[0] = AMD::FmInitVector3(position.x + 0.0f, position.y + 0.0f, position.z + 0.0f);
    obj->restPositions[1] = AMD::FmInitVector3(position.x + 1.0f, position.y + 0.0f, position.z + 0.0f);
    obj->restPositions[2] = AMD::FmInitVector3(position.x + 0.0f, position.y + 1.0f, position.z + 0.0f);
    obj->restPositions[3] = AMD::FmInitVector3(position.x + 0.0f, position.y + 0.0f, position.z + 1.0f);

    obj->tetVertIds[0].ids[0] = 0;
    obj->tetVertIds[0].ids[1] = 1;
    obj->tetVertIds[0].ids[2] = 2;
    obj->tetVertIds[0].ids[3] = 3;

    for (uint i = 0; i < numVerts; ++i) {
        obj->vertIncidentTets[i].Add(0u);
    }

    AMD::FmTetMeshBufferBounds bounds;
    AMD::FmComputeTetMeshBufferBounds(&bounds, nullptr, nullptr, obj->vertIncidentTets, obj->tetVertIds,
                                       nullptr, numVerts, numTets, /*enableFracture=*/false);

    AMD::FmTetMeshBufferSetupParams meshParams;
    meshParams.numVerts = bounds.numVerts;
    meshParams.numTets = bounds.numTets;
    meshParams.numVertIncidentTets = bounds.numVertIncidentTets;
    meshParams.maxVertAdjacentVerts = bounds.maxVertAdjacentVerts;
    meshParams.maxVerts = bounds.maxVerts;
    meshParams.maxExteriorFaces = bounds.maxExteriorFaces;
    meshParams.maxTetMeshes = bounds.maxTetMeshes;
    meshParams.collisionGroup = 0;
    meshParams.enablePlasticity = false;
    meshParams.enableFracture = false;
    meshParams.isKinematic = false;

    obj->tetMeshBuffer = AMD::FmCreateTetMeshBuffer(meshParams, nullptr, nullptr, &obj->tetMesh);
    if (!obj->tetMeshBuffer || !obj->tetMesh) {
        log::get(name())->error("spawnTetrahedron: FmCreateTetMeshBuffer failed");
        return kInvalidHandle;
    }

    AMD::FmMatrix3 identity = AMD::FmInitMatrix3(
        AMD::FmInitVector3(1.0f, 0.0f, 0.0f),
        AMD::FmInitVector3(0.0f, 1.0f, 0.0f),
        AMD::FmInitVector3(0.0f, 0.0f, 1.0f));
    AMD::FmInitVertState(obj->tetMesh, obj->restPositions, identity, AMD::FmInitVector3(0.0f), 1.0f, AMD::FmInitVector3(0.0f));

    // kke::Material -> FmTetMaterialParams, the actual bridge.
    AMD::FmTetMaterialParams femfxMaterial;
    femfxMaterial.restDensity = material.density;
    femfxMaterial.youngsModulus = material.stiffness;
    femfxMaterial.poissonsRatio = material.poissonsRatio;
    femfxMaterial.plasticYieldThreshold = material.plasticYieldThreshold;
    femfxMaterial.plasticCreep = material.plasticCreep;
    femfxMaterial.fractureStressThreshold = material.fractureStressThreshold;

    AMD::FmInitTetState(obj->tetMesh, obj->tetVertIds, femfxMaterial);
    AMD::FmComputeMeshConstantMatrices(obj->tetMesh);

    // A FRESH set of arrays for FmInitConnectivity, not the same
    // vertIncidentTets already passed to FmComputeTetMeshBufferBounds
    // above — that call takes a non-const pointer and, verified by
    // testing, appears to consume/mutate them internally: reusing the
    // same arrays crashed immediately in standalone testing, building
    // a fresh set fixed it. See PhysicsModule.h's class comment for
    // the real, gdb-traced crash this call fixes.
    AMD::FmArray<uint> connectivityVertIncidentTets[4];
    for (uint i = 0; i < numVerts; ++i) {
        connectivityVertIncidentTets[i].Add(0u);
    }
    if (!AMD::FmInitConnectivity(obj->tetMesh, connectivityVertIncidentTets)) {
        log::get(name())->error("spawnTetrahedron: FmInitConnectivity failed");
        AMD::FmDestroyTetMeshBuffer(obj->tetMeshBuffer);
        return kInvalidHandle;
    }
    // NOT calling FmFinishConnectivityFromVertIncidentTets() here —
    // FmInitConnectivity already calls it internally. Doing so a
    // second time was the real cause of a since-fixed crash — see the
    // class comment's debugging account, point 3.
    AMD::FmSetMassesFromRestDensities(obj->tetMesh, 0.0f);

    int initResult = AMD::FmFinishTetMeshInit(obj->tetMesh);
    if (initResult != 0) {
        log::get(name())->error("spawnTetrahedron: FmFinishTetMeshInit failed with code {}", initResult);
        AMD::FmDestroyTetMeshBuffer(obj->tetMeshBuffer);
        return kInvalidHandle;
    }

    AMD::FmEnableSelfCollision(obj->tetMesh, false);
    AMD::FmEnableSleeping(m_scene, obj->tetMesh, false);

    obj->sceneBufferId = AMD::FmAddTetMeshBufferToScene(m_scene, obj->tetMeshBuffer);
    obj->material = material;
    obj->vertexBuffer = std::make_unique<Buffer>(
        m_app->device(), sizeof(Vertex) * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    ObjectHandle handle = m_nextHandle++;
    m_objects[handle] = std::move(obj);
    return handle;
}

void PhysicsModule::removeObject(ObjectHandle handle) {
    auto it = m_objects.find(handle);
    if (it == m_objects.end()) {
        return; // no-op, not an error — see the header's own doc comment
    }
    AMD::FmRemoveTetMeshBufferFromScene(m_scene, it->second->sceneBufferId);
    AMD::FmDestroyTetMeshBuffer(it->second->tetMeshBuffer);
    m_objects.erase(it);
}

void PhysicsModule::fixedUpdate(const FixedUpdateContext& ctx) {
    AMD::FmUpdateScene(m_scene, ctx.fixedDt);

    // Logged every ~1s (at 60Hz), not every tick — see spdlog's own
    // async design: this is exactly the "don't spend even the queue-push
    // cost 60 times a second for something a human glances at
    // occasionally" case.
    if (!m_objects.empty() && ctx.tickIndex % 60 == 0) {
        const auto& first = m_objects.begin()->second;
        AMD::FmVector3 pos = AMD::FmGetVertPosition(*first->tetMesh, 0);
        log::get(name())->info("{} object(s); first object's vert0 height: {:.4f}", m_objects.size(), pos.y);
    }
}

void PhysicsModule::render(const RenderContext& ctx) {
    if (!m_pipeline) {
        return;
    }

    m_pipeline->bind(ctx.cmd);

    // Ground plane — a unit cube scaled to the box half-dims doubled,
    // positioned to match where it was actually created in the scene.
    {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f) * kRenderScale);
        // A thin *visual* slab, not matching the physics ground's full
        // 1-unit collision thickness — purely so a flat floor reads as
        // a floor rather than a thick block from any angle.
        model = glm::scale(model, glm::vec3(100.0f, 0.05f, 100.0f) * kRenderScale);
        glm::mat4 mvp = ctx.proj * ctx.view * model;
        vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);
        m_groundMesh->bind(ctx.cmd);
        m_groundMesh->draw(ctx.cmd);
    }

    // Every spawned object — each one's 4 live simulated positions are
    // already in world space, so the model matrix is identity; see the
    // class comment for why this is the simplest possible render
    // bridge, not a general one.
    glm::mat4 mvp = ctx.proj * ctx.view; // model = identity, shared by every object
    vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);
    vkCmdBindIndexBuffer(ctx.cmd, m_tetIndexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);

    size_t colorIndex = 0;
    for (auto& [handle, obj] : m_objects) {
        glm::vec3 color = kColorPalette[colorIndex % (sizeof(kColorPalette) / sizeof(kColorPalette[0]))];
        ++colorIndex;

        Vertex verts[4];
        for (int i = 0; i < 4; ++i) {
            AMD::FmVector3 p = AMD::FmGetVertPosition(*obj->tetMesh, (uint)i);
            verts[i].position = glm::vec3(p.x, p.y, p.z) * kRenderScale;
            verts[i].color = color;
        }
        obj->vertexBuffer->upload(verts, sizeof(verts));

        VkBuffer buffers[] = { obj->vertexBuffer->handle() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(ctx.cmd, 0, 1, buffers, offsets);
        vkCmdDrawIndexed(ctx.cmd, 12, 1, 0, 0, 0);
    }
}

void PhysicsModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(10, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Physics");

    ImGui::Text("Objects: %zu / %u", m_objects.size(), kMaxObjects);
    ImGui::TextWrapped(
        "This is a live example of PhysicsModule's spawn API, not a "
        "special-cased demo button -- see PhysicsModule::spawnTetrahedron() "
        "for the exact call this makes.");

    if (ImGui::Button("Spawn tetrahedron")) {
        // A little horizontal jitter so consecutive spawns don't land
        // in the exact same spot — genuinely just for a legible demo,
        // not a real placement system.
        float jitterX = static_cast<float>((m_nextHandle * 37) % 200) / 100.0f - 1.0f; // -1..1
        float jitterZ = static_cast<float>((m_nextHandle * 53) % 200) / 100.0f - 1.0f;

        Material material;
        material.density = 700.0f;
        material.stiffness = 1.0e7f;
        material.poissonsRatio = 0.3f;
        material.plasticYieldThreshold = 0.0f;
        material.fractureStressThreshold = 1.0e8f;

        spawnTetrahedron(glm::vec3(jitterX * 3.0f, m_nextSpawnHeight, jitterZ * 3.0f), material);
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear all")) {
        // Copy the keys first — removeObject() erases from m_objects,
        // so iterating that map directly while erasing from it would
        // be exactly the kind of iterator invalidation bug this
        // codebase has otherwise been careful to avoid all session.
        std::vector<ObjectHandle> handles;
        handles.reserve(m_objects.size());
        for (auto& [handle, obj] : m_objects) {
            handles.push_back(handle);
        }
        for (ObjectHandle handle : handles) {
            removeObject(handle);
        }
    }

    ImGui::End();
}

void PhysicsModule::shutdown() {
    // Copy handles first — same reasoning as "Clear all" above.
    std::vector<ObjectHandle> handles;
    handles.reserve(m_objects.size());
    for (auto& [handle, obj] : m_objects) {
        handles.push_back(handle);
    }
    for (ObjectHandle handle : handles) {
        removeObject(handle);
    }

    if (m_ground) {
        AMD::FmDestroyRigidBody(m_ground);
        m_ground = nullptr;
    }
    if (m_scene) {
        AMD::FmDestroyScene(m_scene);
        m_scene = nullptr;
        log::get(name())->info("FEMFX scene destroyed");
    }
}

} // namespace kke

#endif // KKE_ENABLE_FEMFX
